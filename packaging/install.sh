#!/bin/bash
# Moonlight XR Native installer. GPLv3.
#
# Copies the app (binaries, launcher, assets, licences, the Moonlight Qt
# import script) into DEST and adds or updates the Steam library shortcut
# "Moonlight XR Native" through Steam's CEF debugging port. Safe to run again.
#
#   install.sh [--seed] [--no-shortcut] [--art DIR]
#
#   --seed         if ~/.config/moonlight-xr has no client identity yet, import
#                  the pairing from Moonlight Qt (seed-from-moonlight-qt.sh)
#   --no-shortcut  same as NO_SHORTCUT=1
#   --art DIR      shortcut art; DIR holds any of poster, wide, hero, logo and
#                  icon, each as .png/.jpg (default: packaging/art or art if
#                  that directory exists next to this script)
#
# Environment: DEST (default ~/Games/MoonlightXRNative/app), NO_SHORTCUT=1,
# BUILD (source tree only, default <repo>/build), MLXR_CONFIG_DIR.
#
# Runs from a source checkout (binaries from the build directory) or from an
# unpacked release (binaries in app/ next to this script); it tells which.
set -euo pipefail
HERE="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
DEST="${DEST:-$HOME/Games/MoonlightXRNative/app}"
NAME="Moonlight XR Native"
SEED=0; ART=""
[ "${NO_SHORTCUT:-0}" = 1 ] && NOSC=1 || NOSC=0
while [ $# -gt 0 ]; do
    case "$1" in
        --seed) SEED=1 ;;
        --no-shortcut) NOSC=1 ;;
        --art) shift; ART="${1:?--art needs a directory}" ;;
        -h|--help) sed -n '2,/^# unpacked release/p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1 (try --help)"; exit 2 ;;
    esac
    shift
done

if [ -x "$HERE/app/moonlight-xr" ]; then          # unpacked release
    MODE=release
    PAY="$HERE/app"; ASSETS="$PAY/assets"; WRAPPER="$PAY/moonlight-xr.sh"
    CLI="$PAY/mlxr-cli"; MAIN="$PAY/moonlight-xr"; HELLO=""
    SEEDDIR="$PAY"; CEFPY="$HERE/steam_cef.py"; DOCS="$HERE"
    BUGREPORT="$PAY/bug-report.sh"; PROBE="$PAY/docs/probe/SUMMARY.md"
    LAUNCHER_BIN="$PAY/moonlight-xr-launcher"; LAUNCHER_SH="$PAY/moonlight-xr-launcher.sh"
    [ -z "$ART" ] && [ -d "$HERE/art" ] && ART="$HERE/art"
else                                              # source checkout
    MODE=source
    SRC="$(cd "$HERE/.." && pwd)"; BUILD="${BUILD:-$SRC/build}"
    ASSETS="$SRC/assets"; WRAPPER="$HERE/moonlight-xr.sh"
    CLI="$BUILD/backend/mlxr-cli"; MAIN="$BUILD/moonlight-xr"; HELLO="$BUILD/hello/mlxr-hello"
    SEEDDIR="$SRC/backend"; CEFPY="$HERE/steam_cef.py"; DOCS="$SRC"
    BUGREPORT="$SRC/tools/bug-report.sh"; PROBE="$SRC/docs/probe/SUMMARY.md"
    LAUNCHER_BIN="$SRC/build-launcher/moonlight-xr-launcher"; LAUNCHER_SH="$SRC/launcher/moonlight-xr-launcher.sh"
    [ -z "$ART" ] && [ -d "$HERE/art" ] && ART="$HERE/art"
fi
[ -x "$MAIN" ] || { echo "no moonlight-xr binary at $MAIN (run ./build.sh first)"; exit 1; }
[ -x "$CLI" ] || { echo "no mlxr-cli binary at $CLI (run ./build.sh first)"; exit 1; }
[ -d "$ASSETS" ] || { echo "no assets directory at $ASSETS"; exit 1; }
echo "installing ($MODE) to $DEST"

mkdir -p "$DEST"
if [ "$(readlink -f "$DEST")" = "$(readlink -f "$(dirname "$MAIN")")" ] && [ "$MODE" = release ]; then
    echo "  the release was unpacked into DEST itself: nothing to copy"
else
    install -m755 "$MAIN" "$DEST/moonlight-xr";                    echo "  moonlight-xr"
    install -m755 "$CLI" "$DEST/mlxr-cli";                         echo "  mlxr-cli"
    [ -n "$HELLO" ] && [ -x "$HELLO" ] && { install -m755 "$HELLO" "$DEST/mlxr-hello"; echo "  mlxr-hello"; }
    install -m755 "$WRAPPER" "$DEST/moonlight-xr.sh";              echo "  moonlight-xr.sh (VR client wrapper)"
    # The flat Moonlight UI (patched moonlight-qt) that hands streams to the VR client
    if [ -x "$LAUNCHER_BIN" ] && [ -f "$LAUNCHER_SH" ]; then
        install -m755 "$LAUNCHER_BIN" "$DEST/moonlight-xr-launcher"
        install -m755 "$LAUNCHER_SH" "$DEST/moonlight-xr-launcher.sh"; echo "  moonlight-xr-launcher (flat Moonlight UI)"
    fi
    install -m755 "$SEEDDIR/seed-from-moonlight-qt.sh" "$DEST/seed-from-moonlight-qt.sh"
    install -m644 "$SEEDDIR/seed_moonlight_qt.py" "$DEST/seed_moonlight_qt.py"
    echo "  seed-from-moonlight-qt.sh"
    install -m755 "$BUGREPORT" "$DEST/bug-report.sh"
    if [ -f "$PROBE" ]; then mkdir -p "$DEST/docs/probe"; install -m644 "$PROBE" "$DEST/docs/probe/SUMMARY.md"; fi
    echo "  bug-report.sh"
    if [ "$MODE" = release ]; then [ -f "$PAY/VERSION" ] && install -m644 "$PAY/VERSION" "$DEST/VERSION"
    else git -C "$SRC" describe --tags --always --dirty > "$DEST/VERSION" 2>/dev/null || true; fi
    [ -f "$DEST/VERSION" ] && echo "  VERSION ($(cat "$DEST/VERSION"))"
    # mirror assets/, so files removed from a newer release do not linger
    if command -v rsync >/dev/null; then
        rsync -a --delete "$ASSETS/" "$DEST/assets/"
    else
        rm -rf "$DEST/assets.new" && cp -a "$ASSETS" "$DEST/assets.new" && rm -rf "$DEST/assets" && mv "$DEST/assets.new" "$DEST/assets"
    fi
    echo "  assets/ ($(du -sh "$DEST/assets" | cut -f1): rooms, hrtf, models, images, fonts, depth models, licences)"
    for f in LICENSE.txt CREDITS.md; do
        [ -f "$DOCS/$f" ] && install -m644 "$DOCS/$f" "$DEST/$f"
    done
    echo "  LICENSE.txt, CREDITS.md"
fi

# Pairing: only when asked, and never over an existing identity.
CFG="${MLXR_CONFIG_DIR:-$HOME/.config/moonlight-xr}"
if [ "$SEED" = 1 ]; then
    if [ -f "$CFG/client.pem" ] && [ -f "$CFG/client.key" ]; then
        echo "pairing: $CFG already has a client identity, not importing (use seed-from-moonlight-qt.sh --force to replace it)"
    elif "$DEST/seed-from-moonlight-qt.sh"; then
        echo "pairing: imported from Moonlight Qt into $CFG"
    else
        echo "pairing: the Moonlight Qt import failed (see above); pair with: $DEST/mlxr-cli pair <host address>"
    fi
elif [ ! -f "$CFG/client.pem" ]; then
    echo "pairing: no client identity in $CFG yet; run with --seed to import from Moonlight Qt, or: $DEST/mlxr-cli pair <host address>"
fi

if [ "$NOSC" = 1 ]; then echo "Steam shortcut: skipped (NO_SHORTCUT)"; exit 0; fi
if [ ! -f "$CEFPY" ]; then echo "Steam shortcut: $CEFPY missing"; exit 1; fi

CEF="python3 -I $CEFPY"
# The shortcut opens the flat Moonlight UI when it is installed (VR_ONLY=1:
# straight into the VR client's own PC and app lists instead)
if [ "${VR_ONLY:-0}" != 1 ] && [ -x "$DEST/moonlight-xr-launcher" ] && [ -f "$DEST/moonlight-xr-launcher.sh" ]; then
    EXE="\"$DEST/moonlight-xr-launcher.sh\""
else
    EXE="\"$DEST/moonlight-xr.sh\""
fi
JS_NAME=$(printf '%s' "$NAME" | python3 -c 'import json,sys; print(json.dumps(sys.stdin.read()))')
JS_EXE=$(printf '%s' "$EXE" | python3 -c 'import json,sys; print(json.dumps(sys.stdin.read()))')
JS_DIR=$(printf '%s' "\"$DEST\"" | python3 -c 'import json,sys; print(json.dumps(sys.stdin.read()))')
if ! APPID=$($CEF "(async()=>{
  const name=$JS_NAME, exe=$JS_EXE, dir=$JS_DIR;
  const have=[...appStore.allApps].find(a=>a.app_type===1073741824 && a.display_name===name);
  let id = have ? have.appid : await SteamClient.Apps.AddShortcut(name, exe, dir, '');
  SteamClient.Apps.SetShortcutName(id, name);
  SteamClient.Apps.SetShortcutExe(id, exe);
  SteamClient.Apps.SetShortcutStartDir(id, dir);
  SteamClient.Apps.SetShortcutLaunchOptions(id, '');
  return id;
})()"); then
    echo "Steam shortcut: NOT added (is Steam running?). The files are installed; run this script again with Steam up."
    exit 0
fi
echo "Steam shortcut \"$NAME\" appid $APPID"

# Shortcut art: copies into Steam's grid folder under the shortcut's appid.
if [ -n "$ART" ] && [ -d "$ART" ]; then
    n=0
    for g in "$HOME"/.local/share/Steam/userdata/*/config; do
        [ -d "$g" ] || continue
        mkdir -p "$g/grid"
        for kind in poster:p wide: hero:_hero logo:_logo icon:_icon; do
            base="${kind%%:*}"; suf="${kind#*:}"
            for ext in png jpg jpeg; do
                [ -f "$ART/$base.$ext" ] && { install -m644 "$ART/$base.$ext" "$g/grid/$APPID$suf.$ext"; n=$((n+1)); }
            done
        done
    done
    echo "Steam art: $n file(s) copied into the grid folder (Steam shows them after a restart of the library page)"
fi
