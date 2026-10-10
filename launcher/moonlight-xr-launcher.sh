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
# The VR client the launcher starts for a stream
export MLXR_NATIVE_CLIENT="${MLXR_NATIVE_CLIENT:-$HERE/moonlight-xr}"

cd "$HERE" || exit 1
BIN=./moonlight-xr-launcher
[ -x "$BIN" ] || { echo "no $HERE/moonlight-xr-launcher (run launcher/install-launcher.sh)" >&2; exit 1; }
[ -f "$LOGDIR/moonlight-launcher.log" ] && mv -f "$LOGDIR/moonlight-launcher.log" "$LOGDIR/moonlight-launcher.log.1"
{
    echo "== $(date -Is) launching $BIN $* (SteamAppId=${SteamAppId:-none}, client $MLXR_NATIVE_CLIENT)"
    exec "$BIN" "$@"
} >"$LOGDIR/moonlight-launcher.log" 2>&1
