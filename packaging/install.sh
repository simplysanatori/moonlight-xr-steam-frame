#!/bin/bash
# Moonlight XR Native installer: copies the built binaries into
# ~/Games/MoonlightXRNative/app and adds (or updates) the Steam library
# shortcut "Moonlight XR Native" through Steam's CEF debugging port. GPLv3.
set -euo pipefail
SRC="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${BUILD:-$SRC/build}"
DEST="${DEST:-$HOME/Games/MoonlightXRNative/app}"
NAME="Moonlight XR Native"

mkdir -p "$DEST"
install -m755 "$SRC/packaging/moonlight-xr.sh" "$DEST/moonlight-xr.sh"
for b in "$BUILD/hello/mlxr-hello" "$BUILD/moonlight-xr"; do
    [ -x "$b" ] && install -m755 "$b" "$DEST/"
done
[ -d "$SRC/assets" ] && cp -a "$SRC/assets" "$DEST/"
echo "installed to $DEST"

if [ "${NO_SHORTCUT:-0}" = 1 ]; then exit 0; fi
CEF="python3 -I $SRC/packaging/steam_cef.py"
EXE="\"$DEST/moonlight-xr.sh\""
JS_NAME=$(printf '%s' "$NAME" | python3 -c 'import json,sys; print(json.dumps(sys.stdin.read()))')
JS_EXE=$(printf '%s' "$EXE" | python3 -c 'import json,sys; print(json.dumps(sys.stdin.read()))')
JS_DIR=$(printf '%s' "\"$DEST\"" | python3 -c 'import json,sys; print(json.dumps(sys.stdin.read()))')
APPID=$($CEF "(async()=>{
  const name=$JS_NAME, exe=$JS_EXE, dir=$JS_DIR;
  const have=[...appStore.allApps].find(a=>a.app_type===1073741824 && a.display_name===name);
  let id = have ? have.appid : await SteamClient.Apps.AddShortcut(name, exe, dir, '');
  SteamClient.Apps.SetShortcutName(id, name);
  SteamClient.Apps.SetShortcutExe(id, exe);
  SteamClient.Apps.SetShortcutStartDir(id, dir);
  SteamClient.Apps.SetShortcutLaunchOptions(id, '');
  return id;
})()")
echo "Steam shortcut \"$NAME\" appid $APPID"
