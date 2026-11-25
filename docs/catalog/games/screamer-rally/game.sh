# Screamer Rally (1997 / Apr 1998 Voodoo2 Patch) — glide-dosbox config
DEFAULT_MODE="dbx"
DEFAULT_ADMIN_MODE="gng"
DEFAULT_VARIANT="v2"

CDIMG=""      # e.g. "${MEDIA_DIR}/srally.cue"

# Cap CPU cycles during INSTALL.BAT / ST.EXE to avoid exception 00h divide-by-zero
INST_CMD=(-c "cycles fixed 75000" -c "INSTALL.BAT")
CONF_CMD=(-c "cd SR" -c "cycles fixed 75000" -c "ST.EXE" -c "exit")
SHELL_CMD=()

RUN_CMD_v2=(-c "cd SR" -c "SR3DFX2.EXE" -c "exit")
RUN_CMD_v1=(-c "cd SR" -c "STRT3FX.EXE" -c "exit")
RUN_CMD_hires=(-c "cd SR" -c "START65H.EXE" -c "exit")
RUN_CMD_lowres=(-c "cd SR" -c "START65L.EXE" -c "exit")

post_install() {
  strip_glide_ovl ./SR
}
