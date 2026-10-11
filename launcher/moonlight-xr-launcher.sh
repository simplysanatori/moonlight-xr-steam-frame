#!/bin/bash
# Moonlight XR Native flat launcher (the Steam shortcut target once the launcher is installed). GPLv3.
# Moonlight's own UI (PCs, apps, pairing, settings); starting a stream hands off to the native
# VR client (moonlight-xr, next to this script) and the UI comes back when it exits.
# Logs: ~/Games/MoonlightXRNative/moonlight-launcher.log (this program, a fresh one each run, the
# last one kept as .1), moonlight-console.log (the VR client's output, written by the launcher),
# moonlight.log (the VR client's own log).
HERE="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
LOGDIR="${MLXR_LOG_DIR:-$HOME/Games/MoonlightXRNative}"
mkdir -p "$LOGDIR"
export MLXR_LOG_DIR="$LOGDIR"

# Steam injects GL overrides meant for games; they break Mesa for native apps.
unset MESA_GL_VERSION_OVERRIDE MESA_GLSL_VERSION_OVERRIDE
# Steam points LD_LIBRARY_PATH at its runtime; use the host's libraries (Qt6, SDL2, FFmpeg are SteamOS packages).
unset LD_LIBRARY_PATH
# Same runtime rule as moonlight-xr.sh: only set it when the default lookup would fail.
if [ ! -e "$HOME/.config/openxr/1/active_runtime.json" ] && [ -e /opt/steamvr/steamxr_linuxarm64.json ]; then
    export XR_RUNTIME_JSON=/opt/steamvr/steamxr_linuxarm64.json
fi
# Hide the flat window while the VR client streams, as stock Moonlight hides it
# when its stream window opens (SteamVR shows a visible one in front of the VR
# picture); MLXR_LAUNCHER_HIDE_WINDOW=0 keeps it up
export MLXR_LAUNCHER_HIDE_WINDOW="${MLXR_LAUNCHER_HIDE_WINDOW:-1}"
# The VR client the launcher starts for a stream
export MLXR_NATIVE_CLIENT="${MLXR_NATIVE_CLIENT:-$HERE/moonlight-xr}"

cd "$HERE" || exit 1
BIN=./moonlight-xr-launcher
[ -x "$BIN" ] || { echo "no $HERE/moonlight-xr-launcher (run launcher/install-launcher.sh)" >&2; exit 1; }
[ -f "$LOGDIR/moonlight-launcher.log" ] && mv -f "$LOGDIR/moonlight-launcher.log" "$LOGDIR/moonlight-launcher.log.1"
# SteamVR's own frame timing for the whole run (frames the compositor reused,
# dropped or reprojected), a line a second into vrtiming.log, the last run's
# kept as .1: read-only, MLXR_VRTIMING=0 turns it off
PROBE_PID=
if [ "${MLXR_VRTIMING:-1}" != 0 ] && [ -f "$HERE/vrtiming.py" ] && command -v python3 >/dev/null; then
    [ -f "$LOGDIR/vrtiming.log" ] && mv -f "$LOGDIR/vrtiming.log" "$LOGDIR/vrtiming.log.1"
    python3 -I "$HERE/vrtiming.py" 86400 "$LOGDIR/vrtiming.log" >/dev/null 2>&1 &
    PROBE_PID=$!
fi
{
    echo "== $(date -Is) launching $BIN $* (SteamAppId=${SteamAppId:-none}, client $MLXR_NATIVE_CLIENT)"
    "$BIN" "$@"
} >"$LOGDIR/moonlight-launcher.log" 2>&1
rc=$?
[ -n "$PROBE_PID" ] && kill "$PROBE_PID" 2>/dev/null
exit $rc
