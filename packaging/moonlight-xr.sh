#!/bin/bash
# Moonlight XR Native launcher (Steam shortcut target). GPLv3.
# Runs the app from its own directory with the host's libraries.
# Logs (M4): the app writes ~/Games/MoonlightXRNative/moonlight.log itself
# (xr/xr_log.c: appended, rolled over to moonlight.log.1 past 5 MB, level from
# config.ini's file_log); what the libraries print on stdout/stderr (SteamVR's
# client, the Vulkan loader) goes to moonlight-console.log, a fresh one each run.
HERE="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
LOGDIR="${MLXR_LOG_DIR:-$HOME/Games/MoonlightXRNative}"
mkdir -p "$LOGDIR"
CONSOLE="$LOGDIR/moonlight-console.log"
export MLXR_LOG_DIR="$LOGDIR"

# Steam injects GL overrides meant for games; they break Mesa for native apps.
unset MESA_GL_VERSION_OVERRIDE MESA_GLSL_VERSION_OVERRIDE
# Steam points LD_LIBRARY_PATH at its runtime; use the host's libraries (ldd audit target).
unset LD_LIBRARY_PATH
# Only set the runtime explicitly when the default lookup would fail.
if [ ! -e "$HOME/.config/openxr/1/active_runtime.json" ] && [ -e /opt/steamvr/steamxr_linuxarm64.json ]; then
    export XR_RUNTIME_JSON=/opt/steamvr/steamxr_linuxarm64.json
fi

cd "$HERE" || exit 1
BIN=./moonlight-xr
[ -x "$BIN" ] || BIN=./mlxr-hello   # M0: the hello triangle until the app exists
{
    echo "== $(date -Is) launching $BIN $* (SteamAppId=${SteamAppId:-none})"
    exec "$BIN" "$@"
} >"$CONSOLE" 2>&1
