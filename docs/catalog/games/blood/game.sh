# Blood (1997 / Mar 1998 3dfx Beta Patch) — glide-dosbox config
DEFAULT_VARIANT="3dfx"

CDIMG=""      # e.g. "${MEDIA_DIR}/BLOODCD1.cue"
# PATCH_DIR=""

# Retail v1.0 CD uses INSTALL.EXE; One Unit Whole Blood / Plasma Pack discs also ship R0011.EXE / R0020.EXE
INST_CMD=(-c "INSTALL.EXE" -c "cd .." -c "R0011.EXE" -c "R0020.EXE")
CONF_CMD=(-c "cd BLOOD" -c "SETUP.EXE" -c "exit")
SHELL_CMD=()

RUN_CMD_3dfx=(-c "cd BLOOD" -c "SET BUILD_640X480=1" -c "SET BUILD_GAMMA=1" -c "3DFX.EXE" -c "exit")
RUN_CMD_vga=(-c "cd BLOOD" -c "BLOOD.EXE" -c "exit")
