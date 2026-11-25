# DOSBox Glide Launcher (`glide-dosbox`)

`glide-dosbox` (`tools/glide-dosbox/glide-dosbox`) is a modular Bash launcher for managing classic DOS 3dfx titles across DOSBox-SVN (`dosbox_openglide`), DOSBox-X, and DOSBox Staging. It unifies game installation, patch workflows, sound setup, interactive DOS troubleshooting shells, GPU/backend selection, and structured `glide-ng` debug captures behind a single per-game configuration file (`game.sh`) and a portable `./run.sh` symlink.

---

## 1. Overview

Running DOS Glide titles across multiple emulators and wrappers ordinarily requires maintaining bespoke `.conf` files, swapping `GLIDE2X.OVL` binaries between game directories, manually setting `LD_LIBRARY_PATH` or GLVND/PRIME environment variables, and retyping log/HUD/trace arguments for every test pass.

`glide-dosbox` centralizes those concerns:

- **Emulator × Wrapper Separation:** Combines **DOSBox-SVN**, **DOSBox-X**, or **DOSBox Staging** with **glide-ng**, **OpenGLide**, or **built-in software Voodoo** via preset names or orthogonal `--emu=` and `--wrapper=` flags.
- **Full Game Lifecycle:** Supports initialization (`init`), installation (`install`), patch automation (`patch`), sound/input setup (`setup`), interactive DOS prompting (`shell`), and normal play (`run`) or one-off command injection (`--exec`).
- **Symlink-Friendly Invocations:** Works directly on `$PATH` (resolving `$PWD`) or via per-game `./run.sh` symlinks (resolving the symlinked game folder regardless of the caller's working directory).
- **Diagnostics & Safe Overrides:** Applies per-run backend, host GPU, MSAA, card profile, HUD, trace, and screenshot flags without mutating persistent game configuration, and checks for shadowing local `GLIDE2X.OVL` copies before launch.

---

## 2. Setup & Configuration

### Recommended Symlink & User Config Setup

Because `glide-dosbox` resolves its companion `lib/`, `configs/`, `templates/`, and `completions/` directories relative to the real directory behind a symlink, a stable directory symlink lets game directories keep clean, relocatable `./run.sh` links:

```bash
# 1. Link the entire launcher directory into a stable location (for example ~/bin/glide-dosbox)
ln -sfn "$PWD/tools/glide-dosbox" ~/bin/glide-dosbox

# 2. Optional: create user-wide host overrides from the shipped template
mkdir -p "${XDG_CONFIG_HOME:-$HOME/.config}/glide-ng"
cp ~/bin/glide-dosbox/templates/launcher.conf.example \
   "${XDG_CONFIG_HOME:-$HOME/.config}/glide-ng/dosbox-launcher.conf"

# 3. Install dynamic Bash completions for glide-dosbox, ./run.sh, and launch.sh
~/bin/glide-dosbox/glide-dosbox --install-completion

# 4. Verify binaries, Glide libraries, OVL directories, and EGL ICDs
~/bin/glide-dosbox/glide-dosbox doctor
```

### Configuration Precedence & Discovery

When `~/.config/glide-ng/dosbox-launcher.conf` is omitted or leaves variables commented out, `glide-dosbox` automatically discovers:

- `GNG_LIBS` from `<repo>/build_release/lib` or `<repo>/build/lib` (whichever contains `libglide2x.so*`).
- `GNG_OVL_DIR` from `<repo>/build_release/dos` or `<repo>/build/dos` (when built with Open Watcom v2).
- `DOSBOX_SVN` (`dosbox_openglide` / `dosbox-svn`), `DOSBOX_X` (`dosbox-x`), and `DOSBOX_STAGING` (`dosbox-staging` / `dosbox`) from `$PATH`.

Settings resolve in ascending precedence:

1. Auto-discovered repository / `$PATH` defaults
2. Optional sibling `common.sh` (if present beside the launcher)
3. `${GLIDE_DOSBOX_CONF:-${XDG_CONFIG_HOME:-$HOME/.config}/glide-ng/dosbox-launcher.conf}`
4. Caller environment variables (such as `DEFAULT_MODE=dbx`, `GNG_LIBS=...`, `OG_LIBS=...`, `DOSBOX_X=...`)
5. Per-game `./game.sh` (`CDIMG`, `PATCH_DIR`, `*_CMD`, `GAME_ENV`, `DEFAULT_MODE`, `DEFAULT_ADMIN_MODE`; shell `DEFAULT_MODE` / `DEFAULT_ADMIN_MODE` still overrides `./game.sh`)
6. Per-game `./glide-ng.ini` and optional `./dosbox-override.conf`
7. CLI options passed on the `glide-dosbox` / `./run.sh` command line

> Legacy variable names `OGLIBS` (alias for `OG_LIBS`) and `DOSBOX_ST` (alias for `DOSBOX_STAGING`) remain supported in host config files and environment overrides.

---

## 3. Quick Start & Game Lifecycle

Initialize a new game folder and step through install, patch, sound setup, troubleshooting, and launch:

```bash
# Scaffold ./game.sh, link ./run.sh, and copy catalog game.sh / glide-ng.ini / dosbox-override.conf if available
mkdir -p ~/Games/tomb-raider && cd ~/Games/tomb-raider
~/bin/glide-dosbox/glide-dosbox init \
  --cdimg="/path/to/TOMBENG.cue" \
  --mode=gng \
  --catalog=tomb-raider

# Install from D:\ (CDIMG) and automatically execute post_install() + patch phase
./run.sh install

# Or run the patch phase / sound configuration / interactive DOS shell independently
./run.sh patch
./run.sh setup
./run.sh shell --cd

# Launch the game normally, or inspect the exact resolved command first
./run.sh --dry-run
./run.sh
```

### Lifecycle Actions Reference

| Action | Aliases | Purpose | Drives Mounted | Default Start Drive |
| :--- | :--- | :--- | :--- | :--- |
| `run` *(default)* | — | Execute `RUN_CMD` | `C:` (`.` ), `D:` (`CDIMG`), `V:` (`OVL_DIR`) | `C:` (or `D:` with `--cd`) |
| `shell` | `sh`, `dos`, `exec` | Open interactive DOS prompt (`SHELL_CMD`, empty `()` by default; never runs `RUN_CMD`) | `C:`, `D:`, `P:` (`PATCH_DIR`), `V:` | `C:` (or `D:` with `--cd`) |
| `install` | — | Run `INST_CMD`, invoke `post_install()`, then chain `patch` unless `--no-patch` is set | `C:`, `D:`, `P:`, `V:` | `D:` if `CDIMG` set, else `C:` |
| `patch` | — | Run optional `--extract` / `pre_patch()`, `PATCH_CMD`, then `post_patch()` | `C:`, `D:`, `P:`, `V:` | `C:` (or `D:` with `--cd`) |
| `setup` | `conf`, `reconfigure` | Run `CONF_CMD` (sound/controller setup) using `DEFAULT_ADMIN_MODE` | `C:`, `D:`, `V:` | `C:` |
| `init` | — | Generate `./game.sh` (or copy catalog `game.sh`, `glide-ng.ini`, and optional `dosbox-override.conf` via `--catalog`) plus `./run.sh` symlink | — | — |
| `doctor` | `info` | Inspect emulator binaries, libraries, OVL directories, EGL ICDs, and `./game.sh` health | — | — |

### Ad-Hoc Command Execution (`--exec` & `--no-exit`)

To run a diagnostic tool or keep DOSBox open after `RUN_CMD` finishes without modifying `game.sh`:

```bash
# Run a custom command sequence instead of RUN_CMD (stays at DOS prompt afterward)
./run.sh --exec "cd BLOOD" --exec "DIR /W"

# Strip trailing `-c exit` / `-exit` from RUN_CMD so DOSBox stays open after exit
./run.sh --no-exit

# Target a different game directory explicitly from anywhere
glide-dosbox doctor -C /path/to/game
glide-dosbox shell --game-dir=/path/to/game --cd
```

### Runtime Variants (`--variant` / `RUN_CMD_*`)

When a game ships multiple executables or launch modes (for example a 3dfx executable, a Voodoo2 patch binary, and a software VGA hi-res executable or flag), define `RUN_CMD_<NAME>` arrays in `./game.sh` and switch between them cleanly at launch:

```bash
# Inspect available variants defined in ./game.sh (* marks default)
./run.sh --list-variants

# Launch the default variant (`DEFAULT_VARIANT`, or `RUN_CMD` when unset)
./run.sh

# Switch variants on the command line (`--flavor=` / `--flavour=` are aliases)
./run.sh --variant=vga_hires --dry-run
./run.sh --variant=v1 --vk --debug=1
```

Explicit `--variant=NAME` selections append `<NAME>` to auto-generated `./logs/*_<tag>.*` filenames so variant captures do not overwrite default logs. Passing `DEFAULT_VARIANT=<NAME> ./run.sh` overrides `./game.sh`'s default for a single shell invocation.

---

## 4. Profiles & Orthogonal Overrides

Profiles combine an emulator (`svn`, `dbx`, `staging`) with a Glide wrapper target (`gng`, `og`, `hw`). Preset names can be overridden orthogonally with `--emu=` and `--wrapper=`:

| Profile Preset | Emulator | Wrapper / Target | Config Template | Notes |
| :--- | :--- | :--- | :--- | :--- |
| `gng` | `svn` (`DOSBOX_SVN`) | `gng` (`GNG_LIBS` + `GNG_OVL_DIR`) | `configs/dosbox-svn.conf` | Default profile (`lfb = full` for aux depth/backbuffer LFB) |
| `og` | `svn` (`DOSBOX_SVN`) | `og` (`OG_LIBS` + `OG_OVL_DIR`) | `configs/dosbox-svn-og.conf` | Reference comparison against OpenGLide (`lfb = full_noaux`) |
| `dbx` | `dbx` (`DOSBOX_X`) | `gng` (`GNG_LIBS` + `GNG_OVL_DIR`) | `configs/dosbox-x.conf` | Sets `SDL_VIDEODRIVER=x11` by default (`DBX_SDL_VIDEODRIVER=wayland` to override) |
| `dbx-og` | `dbx` (`DOSBOX_X`) | `og` (`OG_LIBS` + `OG_OVL_DIR`) | `configs/dosbox-x.conf` | Run OpenGLide inside DOSBox-X |
| `sw`, `dbx-sw` | `dbx` (`DOSBOX_X`) | `hw` (`HW_OVL_DIR`, built-in SST-1) | `configs/dosbox-x-sw.conf` | DOSBox-X built-in software Voodoo (no `glide-ng`) |
| `st`, `staging` | `staging` (`DOSBOX_STAGING`) | `hw` (`HW_OVL_DIR`, built-in SST-1) | `configs/dosbox-staging.conf` | DOSBox Staging built-in software Voodoo (no `glide-ng`) |

For example, switching OpenGLide from DOSBox-SVN to DOSBox-X only requires adding `--emu=dbx` (equivalent to the `dbx-og` preset):

```bash
./run.sh og --emu=dbx
./run.sh install --emu=dbx
```

Unsupported pairs (`--emu=svn --wrapper=hw`, `--emu=staging --wrapper=gng`, `--emu=staging --wrapper=og`) are rejected before launch with an actionable error.

---

## 5. Backend & Host GPU Selection

When running with `--wrapper=gng` (default), select the `glide-ng` rendering backend and host physical GPU via CLI flags:

- **Rendering backend:** `--vk` / `--vulkan`, `--gl` / `--gles`, `--sw` / `--software`, or `--backend=vk|gl|sw` sets `GLIDE_NG_BACKEND`.
- **Host GPU preference:** `--gpu=default|discrete|integrated|llvmpipe|name:<substr>` sets `GLIDE_NG_GPU` and, on Linux, exports the matching GLVND/PRIME/Mesa variables so OpenGL ES, OpenGLide, and DOSBox's OpenGL output target the same adapter:

| `--gpu=` Value | `GLIDE_NG_GPU` | Additional Linux GLVND / Mesa / PRIME Variables |
| :--- | :--- | :--- |
| `default` *(alias: `os`, `auto`)* | `default` | None — follows OS/loader enumeration order |
| `discrete` *(alias: `dgpu`)* | `discrete` | NVIDIA EGL ICD detected: `__NV_PRIME_RENDER_OFFLOAD=1`, `__GLX_VENDOR_LIBRARY_NAME=nvidia`, `__EGL_VENDOR_LIBRARY_FILENAMES=<nvidia-icd>`; otherwise `DRI_PRIME=1` |
| `integrated` *(alias: `igpu`)* | `integrated` | `DRI_PRIME=0`, `__GLX_VENDOR_LIBRARY_NAME=mesa`, `__EGL_VENDOR_LIBRARY_FILENAMES=<mesa-icd>` |
| `llvmpipe` *(aliases: `llvm`, `cpu`)* | `llvmpipe` | `LIBGL_ALWAYS_SOFTWARE=1`, `GALLIUM_DRIVER=llvmpipe`, `__GLX_VENDOR_LIBRARY_NAME=mesa`, `__EGL_VENDOR_LIBRARY_FILENAMES=<mesa-icd>` |
| `name:<substr>` | `name:<substr>` | Vulkan / GL adapter substring match inside `glide-ng` |

If an explicit GPU target cannot be satisfied or hardware backend creation fails, `glide-ng` logs a warning and falls back safely to the CPU **Software** backend. See [Configuration & Overrides](configuration.md#gpu-selection--os-environment-variables) for standalone non-DOSBox environment details.

```bash
# Force Vulkan on discrete GPU and OpenGL ES on integrated GPU
./run.sh --vk --gpu=discrete
./run.sh --gl --gpu=integrated

# Inspect GLVND + driver behavior on Mesa llvmpipe (or Software fallback)
./run.sh --gl --gpu=llvmpipe --dry-run
```

---

## 6. Debugging & Capture Workflows

Debug scenarios pre-populate telemetry, log, and snapshot filenames under `./logs/` using the active profile, backend tag, and optional `--tag=SUFFIX`:

| Flag | Effect (`./logs/...` auto paths) |
| :--- | :--- |
| `--debug`, `--debug=1`, `--debug=perf` | `GLIDE_NG_HUD=full`, `GLIDE_NG_PERF_LOG=./logs/perf_<tag>.csv`, `GLIDE_NG_LOG_LEVEL=info`, `GLIDE_NG_LOG_FILE=./logs/<tag>.log` |
| `--debug=2`, `--debug=diag`, `--debug=debug` | Scenario 1 + `GLIDE_NG_LOG_LEVEL=debug` + `GLIDE_NG_LOG_CONSOLE=1` |
| `--debug=3[:A-B]`, `--debug=trace[:A-B]` | Scenario 1 + `GLIDE_NG_LOG_LEVEL=trace`, categories `api,lfb,tex,perf`, `GLIDE_NG_TRACE_FRAMES=A-B` (default `100-105`), `./logs/trace_<tag>.log` |
| `--debug=4`, `--debug=visual`, `--debug=snap` | Scenario 1 + `FX_GLIDE_SNAPSHOT=./logs/shots_<tag>/@60+60:15` |
| `--debug=wsi` | `GLIDE_NG_LOG_LEVEL=debug`, categories `wsi,general`, console mirroring, and windowed mode (`-w`) |

Individual override flags can refine or bypass any scenario field:

- `--hud[=SPEC]`, `--perf[=FILE]`, `--log[=LEVEL]`, `--log-file=FILE`, `--console`, `--tag=SUFFIX` / `--suffix=SUFFIX`
- `--trace=A-B`, `--snap=SPEC`
- `-w` / `--windowed` (layers `configs/dosbox-windowed.conf` on top of `./dosbox-override.conf`)
- `--uncap` (`GLIDE_NG_VSYNC=0`, `GLIDE_NG_MAX_FPS=0`), `--msaa=N`, `--no-splash`, `--card=MODEL`, `--tmu-mem=MB`
- `--dry-run` (prints the full `env` and DOSBox invocation without spawning DOSBox or creating `./logs/`)

---

## 7. Authoring `game.sh` & Lifecycle Hooks

Each game directory contains a small Bash config file (`./game.sh`; see `tools/glide-dosbox/templates/game.sh.template`).

### Game Variables

| Variable | Type | Purpose |
| :--- | :--- | :--- |
| `DEFAULT_MODE`, `DEFAULT_ADMIN_MODE` | String | Per-game default profile (`gng`, `og`, `dbx`, `dbx-og`, `sw`, `staging`) for `run` and admin actions (`install`, `patch`, `setup`, `shell`). |
| `DEFAULT_VARIANT` | String | Default key for `run` when `RUN_CMD_<NAME>` variants are defined (overridable via `DEFAULT_VARIANT=<NAME>` in env or `--variant=NAME`). |
| `CDIMG` | String | Path to ISO/CUE image mounted as `D:` (`imgmount d "..." -t iso`). Optional `MEDIA_DIR` from `dosbox-launcher.conf` is available inside `game.sh`. |
| `PATCH_DIR` | String | Host directory mounted as `P:` during `install`, `patch`, and `shell`. |
| `INST_CMD`, `CONF_CMD`, `PATCH_CMD`, `SHELL_CMD`, `RUN_CMD` | Arrays | DOSBox `-c` commands for each action (`SHELL_CMD=()` defaults to empty so `shell` drops directly to `C:\` or `D:\`). |
| `RUN_CMD_<NAME>`, `RUN_ENV_<NAME>`, `RUN_MODE_<NAME>` | Arrays / String | Named runtime variants selected by `--variant=NAME` or `DEFAULT_VARIANT`, with optional per-variant env overrides (applied after `GAME_ENV`) and default profile override. |
| `PATCH_ARCHIVE` / `PATCH_ARCHIVES`, `PATCH_DEST` | String / Array | Optional archives extracted when `./run.sh patch --extract` is used. |
| `GAME_ENV` | Array | Persistent environment variables (`KEY=VAL`) passed across every launcher mode (prefer `./glide-ng.ini` for `glide-ng` settings). |

### Built-in Hook Helpers & Local `GLIDE2X.OVL` Safety

`game.sh` can define optional Bash functions `post_install()`, `pre_patch()`, and `post_patch()`. Three helper functions are built into the launcher environment for use inside those hooks:

- `extract_patch <archive> <dest_dir>` — extracts `.tar.*`, `.zip`, `.7z`, or `.rar` archives into `<dest_dir>`.
- `backup_file <path>` — idempotently copies `<path>` to `<path>.bak` if no backup exists yet.
- `strip_glide_ovl [dir]` — case-insensitively locates and removes local `GLIDE2X.OVL` copies under `[dir]` (default `.`) so the wrapper-managed `V:\GLIDE2X.OVL` is not shadowed.

Example `game.sh` combining CD install, patch archive extraction, DOS patch execution, and OVL cleanup:

```bash
DEFAULT_ADMIN_MODE="dbx"
CDIMG="${MEDIA_DIR}/GAME/GAME.cue"
PATCH_DIR="${MEDIA_DIR}/GAME/patches"

INST_CMD=(-c "INSTALL.EXE")
CONF_CMD=(-c "cd GAME" -c "SETSOUND.EXE" -c "exit")
PATCH_CMD=(-c "cd GAME" -c "PATCH12.EXE" -c "exit")
SHELL_CMD=(-c "cd GAME")
RUN_CMD=(-c "cd GAME" -c "GAME3DFX.EXE" -c "exit")

post_install() {
  backup_file ./GAME/GAME3DFX.EXE
  extract_patch "${PATCH_DIR}/game_12_3dfx.tar.zst" ./GAME
}

post_patch() {
  strip_glide_ovl ./GAME
}
```

When launching `gng` or `og` (`run` or `shell`), `glide-dosbox` also scans up to depth 3 inside the game tree and emits a stderr warning if a local `GLIDE2X.OVL` still shadows DOSBox's mounted `V:\` pass-through overlay.

---

## 8. Extending the Launcher

The implementation under `tools/glide-dosbox/` is split into focused modules so profiles, GPU rules, or new helper commands can be added without modifying unrelated logic:

| Path | Responsibility |
| :--- | :--- |
| `tools/glide-dosbox/glide-dosbox` | Entrypoint, symlink-aware `GAME_DIR` resolution, argument parsing, DOSBox command assembly |
| `tools/glide-dosbox/lib/config.sh` | Repo/`PATH` auto-discovery, `dosbox-launcher.conf` loading, legacy alias mapping, `game.sh` loading |
| `tools/glide-dosbox/lib/profiles.sh` | `--emu` × `--wrapper` matrix validation and preset mapping |
| `tools/glide-dosbox/lib/gpu.sh` | `--gpu=` parsing and Linux GLVND/PRIME/Mesa ICD lookup |
| `tools/glide-dosbox/lib/debug.sh` | `--debug=` scenario defaults, `./logs/` path formatting, `GLIDE_NG_*` CLI env generation |
| `tools/glide-dosbox/lib/helpers.sh` | Hook utilities (`extract_patch`, `backup_file`, `strip_glide_ovl`), exit stripping, local OVL checks |
| `tools/glide-dosbox/lib/variants.sh` | Runtime variant discovery, lookup, and `RUN_CMD_*` / `RUN_ENV_*` / `RUN_MODE_*` resolution |
| `tools/glide-dosbox/lib/commands.sh` | `--help`, `--install-completion`, `init`, and `doctor` |
| `tools/glide-dosbox/configs/` | Minimal shared DOSBox configs (`dosbox-svn.conf`, `dosbox-svn-og.conf`, `dosbox-x.conf`, `dosbox-x-sw.conf`, `dosbox-staging.conf`, `dosbox-windowed.conf`) |
| `tools/glide-dosbox/completions/glide-dosbox.bash` | Context-aware Bash completion including dynamic `--catalog=` lookup |

> Note: On Linux distributions where `dosbox-x` or `dosbox-svn` carries file capabilities (such as `cap_net_admin,cap_net_raw=eip`), `glide-dosbox doctor` reports a `[WARN]` because glibc `AT_SECURE=1` strips `LD_LIBRARY_PATH` and causes dynamic wrapper selection (`GNG_LIBS` / `OG_LIBS`) to fall back to the system library.

Additionally, `configs/dosbox-x.conf` and `configs/dosbox-x-sw.conf` enable `use dynamic core with fpu = true` because DOSBox-X's default `auto` disables x87 FPU translation in `dynamic_rec`, forcing every FPU instruction in 3D DOS Glide games back to the normal interpreter.
