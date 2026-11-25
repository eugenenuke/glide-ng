# Grand Theft Auto (1997 / DOS 3dfx) — glide-dosbox config
# The CD installer shells out to XCOPY, which is absent in DOSBox-SVN.
DEFAULT_ADMIN_MODE="dbx"
DEFAULT_VARIANT="3dfx"

CDIMG=""      # e.g. "${MEDIA_DIR}/GTA.cue"

INST_CMD=(-c "INSTALL.BAT")
CONF_CMD=(-c "cd GTA/GTADOS" -c "K.EXE" -c "exit")
SHELL_CMD=()

RUN_CMD_3dfx=(-c "cd GTA/GTADOS" -c "GTAFX.EXE" -c "exit")
RUN_CMD_24bit=(-c "cd GTA/GTADOS" -c "GTA24.EXE" -c "exit")
RUN_CMD_8bit=(-c "cd GTA/GTADOS" -c "GTA8.EXE" -c "exit")
