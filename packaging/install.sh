#!/bin/bash
# Moonlight XR for Frame installer. GPLv3.
#
# Copies the app (binaries, launcher, assets, licences, the Moonlight Qt
# import script) into DEST and adds or updates the Steam library shortcut
# "Moonlight XR for Frame" through Steam's CEF debugging port, and puts its art
# in Steam's grid folder. Safe to run again.
#
#   install.sh [--seed] [--no-shortcut] [--art DIR]
#
#   --seed         if ~/.config/moonlight-xr has no client identity yet, import
#                  the pairing from Moonlight Qt (seed-from-moonlight-qt.sh)
#   --no-shortcut  same as NO_SHORTCUT=1
#   --art DIR      shortcut art; DIR holds any of portrait (or poster), wide,
#                  hero, logo and icon, as <name>*.png/.jpg (default: the
#                  branding art in assets/branding)
#
# Environment: DEST (default ~/Games/MoonlightXRNative/app), NO_SHORTCUT=1,
# MLXR_INSTALL_LOG (default ~/Games/MoonlightXRNative/install.log),
# BUILD (source tree only, default <repo>/build), MLXR_CONFIG_DIR,
# MLXR_FRAME_RENDERMODELS (where SteamVR keeps the Frame controller models,
# default /opt/steamvr/drivers/frame_controller/resources/rendermodels).
#
# The Steam Frame's own controller models are made here from the local SteamVR
# install (frame-controller-models.py into DEST/assets/models) and are never
# part of a release or of git; without SteamVR or GdkPixbuf the client draws no
# Frame controller and the install carries on.
#
# It never asks a question and never reads the terminal, so another program (the community installers
# Frametanium and FrameDrop) can run it with no terminal and without Steam running. Everything it prints is
# also appended to the log file. When Steam cannot be reached it installs the files, says that the Steam
# shortcut was skipped and exits 0; run it again with Steam up to add the shortcut. The last two lines of the
# output are the summary (some callers show only those).
#
# Runs from a source checkout (binaries from the build directory) or from an
# unpacked release (binaries in app/ next to this script); it tells which.
set -euo pipefail
SELF="$(readlink -f "$0")"
HERE="$(cd "$(dirname "$SELF")" && pwd)"
# Log file: run again as a child with stdout and stderr copied into it. The pipe makes this wait for the copy,
# so the last lines are in the caller's output and in the log before this exits. With no writable log the
# install goes on without one.
LOG="${MLXR_INSTALL_LOG:-$HOME/Games/MoonlightXRNative/install.log}"
if [ -z "${MLXR_INSTALL_INNER:-}" ]; then
    case " $* " in *" -h "*|*" --help "*) ;; *)
        if mkdir -p "$(dirname "$LOG")" 2>/dev/null \
           && { [ ! -f "$LOG" ] || [ "$(stat -c %s "$LOG" 2>/dev/null || echo 0)" -lt 1048576 ] || mv -f "$LOG" "$LOG.1"; } \
           && : >> "$LOG" 2>/dev/null; then
            set +e
            MLXR_INSTALL_INNER=1 MLXR_INSTALL_TTY="$([ -t 0 ] && echo yes || echo none)" bash "$SELF" "$@" 2>&1 </dev/null | tee -a "$LOG"
            rc=${PIPESTATUS[0]}
            exit "$rc"
        fi
        echo "log file $LOG is not writable: installing without one" ;;
    esac
fi
exec </dev/null   # nothing below asks anything; a closed or missing terminal changes nothing
if [ -n "${MLXR_INSTALL_INNER:-}" ]; then
    echo "== $(date '+%Y-%m-%d %H:%M:%S') install.sh $* (user $(id -un), terminal: ${MLXR_INSTALL_TTY:-none}, log: $LOG)"
fi
DEST="${DEST:-$HOME/Games/MoonlightXRNative/app}"
NAME="Moonlight XR for Frame"
OLDNAME="Moonlight XR Native"   # the shortcut's name until it was renamed; it is renamed in place
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
    FRAMEMODELS="$HERE/frame-controller-models.py"
    CLI="$PAY/mlxr-cli"; MAIN="$PAY/moonlight-xr"; HELLO=""; PADPROBE="$PAY/mlxr-gamepad-probe"
    SEEDDIR="$PAY"; CEFPY="$HERE/steam_cef.py"; DOCS="$HERE"
    BUGREPORT="$PAY/bug-report.sh"; PROBE="$PAY/docs/probe/SUMMARY.md"; VRTIMING="$PAY/vrtiming.py"
    LAUNCHER_BIN="$PAY/moonlight-xr-launcher"; LAUNCHER_SH="$PAY/moonlight-xr-launcher.sh"
    [ -z "$ART" ] && [ -d "$HERE/art" ] && ART="$HERE/art"
else                                              # source checkout
    MODE=source
    SRC="$(cd "$HERE/.." && pwd)"; BUILD="${BUILD:-$SRC/build}"
    FRAMEMODELS="$SRC/tools/frame-models/frame-controller-models.py"
    ASSETS="$SRC/assets"; WRAPPER="$HERE/moonlight-xr.sh"
    CLI="$BUILD/backend/mlxr-cli"; MAIN="$BUILD/moonlight-xr"; HELLO="$BUILD/hello/mlxr-hello"; PADPROBE="$BUILD/input/mlxr-gamepad-probe"
    SEEDDIR="$SRC/backend"; CEFPY="$HERE/steam_cef.py"; DOCS="$SRC"
    BUGREPORT="$SRC/tools/bug-report.sh"; PROBE="$SRC/docs/probe/SUMMARY.md"; VRTIMING="$SRC/tools/vrtiming.py"
    LAUNCHER_BIN="$SRC/build-launcher/moonlight-xr-launcher"; LAUNCHER_SH="$SRC/launcher/moonlight-xr-launcher.sh"
    [ -z "$ART" ] && [ -d "$HERE/art" ] && ART="$HERE/art"
fi
[ -z "$ART" ] && [ -d "$ASSETS/branding" ] && ART="$ASSETS/branding"
[ -x "$MAIN" ] || { echo "no moonlight-xr binary at $MAIN (run ./build.sh first)"; exit 1; }
[ -x "$CLI" ] || { echo "no mlxr-cli binary at $CLI (run ./build.sh first)"; exit 1; }
[ -d "$ASSETS" ] || { echo "no assets directory at $ASSETS"; exit 1; }
echo "installing ($MODE) to $DEST"
# The last two lines of every successful run: what was installed and what happened to the Steam shortcut.
finish() {   # finish <shortcut status>
    local v; v="$(cat "$DEST/VERSION" 2>/dev/null | head -n 1 | cut -d' ' -f1)"
    echo "$NAME ${v:+$v }installed."
    echo "$1"
    exit 0
}
tilde() { printf '%s' "${1/#$HOME/\~}"; }

mkdir -p "$DEST"
if [ "$(readlink -f "$DEST")" = "$(readlink -f "$(dirname "$MAIN")")" ] && [ "$MODE" = release ]; then
    echo "  the release was unpacked into DEST itself: nothing to copy"
else
    install -m755 "$MAIN" "$DEST/moonlight-xr";                    echo "  moonlight-xr"
    install -m755 "$CLI" "$DEST/mlxr-cli";                         echo "  mlxr-cli"
    [ -n "$HELLO" ] && [ -x "$HELLO" ] && { install -m755 "$HELLO" "$DEST/mlxr-hello"; echo "  mlxr-hello"; }
    # Lists the SDL gamepads as the client classifies them (docs/dev/GAMEPAD_CERT.md)
    [ -x "$PADPROBE" ] && { install -m755 "$PADPROBE" "$DEST/mlxr-gamepad-probe"; echo "  mlxr-gamepad-probe"; }
    install -m755 "$WRAPPER" "$DEST/moonlight-xr.sh";              echo "  moonlight-xr.sh (VR client wrapper)"
    # The flat Moonlight UI (patched moonlight-qt) that hands streams to the VR client
    if [ -x "$LAUNCHER_BIN" ] && [ -f "$LAUNCHER_SH" ]; then
        install -m755 "$LAUNCHER_BIN" "$DEST/moonlight-xr-launcher"
        install -m755 "$LAUNCHER_SH" "$DEST/moonlight-xr-launcher.sh"; echo "  moonlight-xr-launcher (flat Moonlight UI)"
        # It asks Steam to put its flat screen back up in the headset after a stream
        [ -f "$CEFPY" ] && install -m644 "$CEFPY" "$DEST/steam_cef.py"
    fi
    install -m755 "$SEEDDIR/seed-from-moonlight-qt.sh" "$DEST/seed-from-moonlight-qt.sh"
    install -m644 "$SEEDDIR/seed_moonlight_qt.py" "$DEST/seed_moonlight_qt.py"
    echo "  seed-from-moonlight-qt.sh"
    install -m755 "$BUGREPORT" "$DEST/bug-report.sh"
    [ -f "$VRTIMING" ] && install -m755 "$VRTIMING" "$DEST/vrtiming.py" && echo "  vrtiming.py (SteamVR frame timing, logged by the launcher)"
    if [ -f "$PROBE" ]; then mkdir -p "$DEST/docs/probe"; install -m644 "$PROBE" "$DEST/docs/probe/SUMMARY.md"; fi
    echo "  bug-report.sh"
    if [ "$MODE" = release ]; then [ -f "$PAY/VERSION" ] && install -m644 "$PAY/VERSION" "$DEST/VERSION"
    else echo "$(head -n 1 "$SRC/VERSION" 2>/dev/null || echo dev) (source $(git -C "$SRC" describe --tags --always --dirty 2>/dev/null))" > "$DEST/VERSION"; fi
    [ -f "$DEST/VERSION" ] && echo "  VERSION ($(cat "$DEST/VERSION"))"
    # mirror assets/, so files removed from a newer release do not linger
    if command -v rsync >/dev/null; then
        rsync -a --delete "$ASSETS/" "$DEST/assets/"
    else
        rm -rf "$DEST/assets.new" && cp -a "$ASSETS" "$DEST/assets.new" && rm -rf "$DEST/assets" && mv "$DEST/assets.new" "$DEST/assets"
    fi
    # never Frame models that came with the source tree's assets: they are made below, from SteamVR
    rm -f "$DEST"/assets/models/frame_controller_*
    echo "  assets/ ($(du -sh "$DEST/assets" | cut -f1): rooms, hrtf, models, images, fonts, depth models, licences)"
    for f in LICENSE.txt CREDITS.md; do
        [ -f "$DOCS/$f" ] && install -m644 "$DOCS/$f" "$DEST/$f"
    done
    echo "  LICENSE.txt, CREDITS.md"
fi

# The Steam Frame's own controller models, converted from the SteamVR on this
# machine (Valve's files are read here and never redistributed). Optional: any
# failure leaves no Frame model (the client draws none) and the install going.
FM_SRC="${MLXR_FRAME_RENDERMODELS:-/opt/steamvr/drivers/frame_controller/resources/rendermodels}"
rm -f "$DEST"/assets/models/frame_controller_* # remade each install, so a SteamVR that went away leaves none
if [ ! -f "$FRAMEMODELS" ]; then
    echo "Frame controller models: converter missing ($FRAMEMODELS), skipped (Show controllers Frame draws no model)"
elif [ ! -d "$FM_SRC" ]; then
    echo "Frame controller models: no SteamVR controller models at $FM_SRC, skipped (Show controllers Frame draws no model)"
elif python3 -I "$FRAMEMODELS" "$DEST/assets/models" "$FM_SRC"; then
    echo "  Frame controller models (made from the local SteamVR; the Settings switch for controller models draws them)"
else
    rm -f "$DEST"/assets/models/frame_controller_*
    echo "Frame controller models: conversion failed (see above; GdkPixbuf for Python is needed), skipped (Show controllers Frame draws no model)"
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

if [ "$NOSC" = 1 ]; then echo "Steam shortcut: skipped (NO_SHORTCUT)"; finish "Steam shortcut skipped (asked for)."; fi
if [ ! -f "$CEFPY" ]; then echo "Steam shortcut: $CEFPY missing"; exit 1; fi

CEF="python3 -I $CEFPY"
# The icon in SteamVR's dock is Steam's own icon for the shortcut (the icon field in shortcuts.vdf, which
# Steam turns into icon data when it starts; the dock asks Steam for it by appid). What this adds for the dock
# (the VR client's SteamVR app manifest, the launcher window's icon-theme PNGs and
# .desktop file); see dock_icon.py. Needs the shortcut's appid, which it finds in shortcuts.vdf by itself.
dock_icon() {
    local py="$HERE/dock_icon.py"
    [ -f "$py" ] || { echo "dock icon: $py missing, skipped"; return 0; }
    python3 -I "$py" install "$DEST" "$@" || echo "dock icon: not set up yet (see above); the rest of the install is done"
}
# The shortcut opens the flat Moonlight UI when it is installed (VR_ONLY=1:
# straight into the VR client's own PC and app lists instead)
if [ "${VR_ONLY:-0}" != 1 ] && [ -x "$DEST/moonlight-xr-launcher" ] && [ -f "$DEST/moonlight-xr-launcher.sh" ]; then
    EXE="\"$DEST/moonlight-xr-launcher.sh\""
else
    EXE="\"$DEST/moonlight-xr.sh\""
fi
JS_NAME=$(printf '%s' "$NAME" | python3 -c 'import json,sys; print(json.dumps(sys.stdin.read()))')
JS_OLDNAME=$(printf '%s' "$OLDNAME" | python3 -c 'import json,sys; print(json.dumps(sys.stdin.read()))')
JS_EXE=$(printf '%s' "$EXE" | python3 -c 'import json,sys; print(json.dumps(sys.stdin.read()))')
JS_DIR=$(printf '%s' "\"$DEST\"" | python3 -c 'import json,sys; print(json.dumps(sys.stdin.read()))')
# The shortcut is found by what it runs (the launcher or VR wrapper in DEST), not by its name, so
# renaming it never makes a second one, and its appid (which SteamVR's per-app settings, the game
# resolution and the art are keyed by) stays. Then by the new or the old name.
HAVE_ID=""
for v in "$HOME"/.local/share/Steam/userdata/*/config/shortcuts.vdf; do
    [ -f "$v" ] || continue
    HAVE_ID=$(python3 -I -c '
import re, struct, sys
d = open(sys.argv[1], "rb").read()
dest = sys.argv[2]
for m in re.finditer(rb"\x02appid\x00(....)\x01AppName\x00[^\x00]*\x00\x01Exe\x00([^\x00]*)\x00", d, re.S):
    exe = m.group(2).decode(errors="replace").strip(chr(34))
    if exe in (dest + "/moonlight-xr-launcher.sh", dest + "/moonlight-xr.sh"):
        print(struct.unpack("<I", m.group(1))[0]); break
' "$v" "$DEST") && [ -n "$HAVE_ID" ] && break
done
if ! APPID=$($CEF "(async()=>{
  const name=$JS_NAME, oldname=$JS_OLDNAME, exe=$JS_EXE, dir=$JS_DIR, known='$HAVE_ID';
  const shortcuts=[...appStore.allApps].filter(a=>a.app_type===1073741824);
  const have=(known && shortcuts.find(a=>String(a.appid)===known)) || shortcuts.find(a=>a.display_name===name) || shortcuts.find(a=>a.display_name===oldname);
  let id = have ? have.appid : await SteamClient.Apps.AddShortcut(name, exe, dir, '');
  SteamClient.Apps.SetShortcutName(id, name);
  SteamClient.Apps.SetShortcutExe(id, exe);
  SteamClient.Apps.SetShortcutStartDir(id, dir);
  SteamClient.Apps.SetShortcutLaunchOptions(id, '');
  return id;
})()"); then
    echo "Steam shortcut: SKIPPED, Steam could not be reached (is Steam running?). The files are installed."
    dock_icon
    finish "Steam shortcut skipped (Steam not reachable): start Steam, then run $(tilde "$SELF") again."
fi
echo "Steam shortcut \"$NAME\" appid $APPID"
# Steam's game resolution for the flat window, 1080p (the owner found 1440p too small to read; gamescope's own default is 1280x720
# on a 1920x1080 screen)
$CEF "SteamClient.Apps.SetAppResolutionOverride($APPID,'1920x1080')" >/dev/null 2>&1 \
    && echo "Steam game resolution: 1920x1080" || echo "Steam game resolution: not set"

# SteamVR's per-app video settings for the shortcut, as the owner has them for
# Moonlight XR: 90 Hz (the Frame's default for an app is 72), motion smoothing
# off, render resolution 2160, SteamVR's own sharpening off. Set through the
# running SteamVR (vrcmd), which keeps steamvr.vrsettings itself; without it
# running they are set on the next install.
VRCMD=/opt/steamvr/bin/linuxarm64/vrcmd
if [ -x "$VRCMD" ] && pgrep -x vrserver >/dev/null; then
    ok=1
    for kv in preferredRefreshRate:90 motionSmoothingOverride:0 resolutionOverride:2160 sharpeningOverride:0; do
        env -u LD_LIBRARY_PATH timeout 20 "$VRCMD" --set-settings-int "steam.app.$APPID.${kv%%:*}" "${kv##*:}" >/dev/null 2>&1 || ok=0
    done
    [ "$ok" = 1 ] && echo "SteamVR per-app settings: 90 Hz, motion smoothing off, resolution 2160, sharpening off" \
        || echo "SteamVR per-app settings: could not set them all (set 90 Hz in SteamVR's per-app video settings)"
else
    echo "SteamVR per-app settings: SteamVR is not running; run this again with it up to set 90 Hz"
fi

# Shortcut art, for this shortcut's appid only: copied into Steam's grid folder under the names Steam
# reads (<appid>p portrait, <appid> wide, <appid>_hero, <appid>_logo, <appid>_icon), then handed to the
# running Steam too (SetCustomArtworkForApp: 0 portrait, 1 hero, 2 logo, 3 wide) so the library shows it
# without a restart, and the shortcut's icon is set. A file that is already the same is left alone.
if [ -n "$ART" ] && [ -d "$ART" ]; then
    n=0; ICON=""
    pick() { # pick <name prefix>...: the first file in ART that starts with one of them
        local pre f
        for pre in "$@"; do
            for f in "$ART/$pre"*.png "$ART/$pre"*.jpg "$ART/$pre"*.jpeg; do [ -f "$f" ] && { echo "$f"; return 0; }; done
        done
        return 0
    }
    for g in "$HOME"/.local/share/Steam/userdata/*/config; do
        [ -d "$g" ] || continue
        mkdir -p "$g/grid"
        for kind in "portrait poster:p" "wide:" "hero:_hero" "logo:_logo" "icon-512 icon:_icon"; do
            pat="${kind%%:*}"; suf="${kind#*:}"
            src=$(pick $pat)
            [ -n "$src" ] || continue
            dst="$g/grid/$APPID$suf.${src##*.}"
            cmp -s "$src" "$dst" 2>/dev/null || { install -m644 "$src" "$dst"; n=$((n+1)); }
            [ "$suf" = _icon ] && [ -z "$ICON" ] && ICON="$dst"
        done
    done
    echo "Steam art: $n file(s) updated in the grid folder"
    if [ -n "$ICON" ]; then
        JS_ICON=$(printf '%s' "$ICON" | python3 -c 'import json,sys; print(json.dumps(sys.stdin.read()))')
        $CEF "(()=>{SteamClient.Apps.SetShortcutIcon($APPID,$JS_ICON); return 1})()" >/dev/null 2>&1 \
            && echo "Steam shortcut icon: $ICON (Steam loads a shortcut's icon data only at start: SteamVR's dock shows it after the next Steam restart)" || echo "Steam shortcut icon: not set"
    fi
    for spec in "portrait poster:0" "hero:1" "logo:2" "wide:3"; do
        src=$(pick ${spec%%:*})
        [ -n "$src" ] || continue
        case "${src##*.}" in png) mime=png ;; *) mime=jpeg ;; esac
        python3 -I -c '
import base64, sys
b = base64.b64encode(open(sys.argv[1], "rb").read()).decode()
print("(async()=>{ await SteamClient.Apps.SetCustomArtworkForApp(%s, %r, %r, %s); return 1 })()" % (sys.argv[2], b, sys.argv[3], sys.argv[4]))
' "$src" "$APPID" "$mime" "${spec##*:}" | $CEF - >/dev/null 2>&1 || echo "Steam art: Steam did not take ${src##*/} (it is in the grid folder; it shows after a Steam restart)"
    done
fi

dock_icon --appid "$APPID"
finish "Steam shortcut ready (appid $APPID). Log: $(tilde "$LOG")"
