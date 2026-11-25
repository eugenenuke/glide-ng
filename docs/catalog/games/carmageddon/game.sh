# Carmageddon (1997 / 3dfx & Voodoo2 Patches) — glide-dosbox config
DEFAULT_VARIANT="v2"

CDIMG=""      # e.g. "${MEDIA_DIR}/CARMA.cue"
# PATCH_DIR="" # optional host directory containing CARV24ROW.ZIP / Voodoo2 patch

INST_CMD=(-c "DOSINST.BAT")
CONF_CMD=(-c "cd GAMES/CARMA" -c "SNDSETUP.EXE" -c "exit")
SHELL_CMD=()

RUN_CMD_v2=(-c "cd GAMES/CARMA" -c "Voodo2C.exe" -c "exit")
RUN_CMD_v1=(-c "cd GAMES/CARMA" -c "CARMAV.EXE" -c "exit")
RUN_ENV_v1=(GLIDE_NG_CARD_MODEL=VoodooGraphics)
RUN_CMD_hires=(-c "cd GAMES/CARMA" -c "CARMA.EXE -hires" -c "exit")
RUN_CMD_lowres=(-c "cd GAMES/CARMA" -c "CARMA.EXE" -c "exit")

post_install() {
  # Uncomment and adjust filenames if applying extracted patch archives automatically:
  # extract_patch "${PATCH_DIR}/CARV24ROW.ZIP" ./GAMES/CARMA
  # extract_patch "${PATCH_DIR}/carma_voodoo2.zip" ./GAMES/CARMA
  rm -f ./GAMES/CARMA/Voodo2S.exe
  strip_glide_ovl ./GAMES/CARMA
}
