#!/bin/bash
# Installs the flat launcher next to the native VR client. GPLv3.
#   launcher/install-launcher.sh            DEST=~/Games/MoonlightXRNative/app, BUILD_DIR=build-launcher
# Copies build-launcher/moonlight-xr-launcher and launcher/moonlight-xr-launcher.sh into DEST (where
# packaging/install.sh puts moonlight-xr and moonlight-xr.sh). It does not touch Steam shortcuts: the
# shortcut's target becomes DEST/moonlight-xr-launcher.sh once the owner wants the flat UI first
# (shortcut 2761093165 "Moonlight XR Native" runs moonlight-xr.sh, the in-headset lists, until then).
set -euo pipefail
SRC="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${BUILD_DIR:-$SRC/build-launcher}"
DEST="${DEST:-$HOME/Games/MoonlightXRNative/app}"
[ -x "$BUILD/moonlight-xr-launcher" ] || { echo "no $BUILD/moonlight-xr-launcher: run launcher/build.sh first"; exit 1; }
mkdir -p "$DEST"
install -m755 "$BUILD/moonlight-xr-launcher" "$DEST/moonlight-xr-launcher"
install -m755 "$SRC/launcher/moonlight-xr-launcher.sh" "$DEST/moonlight-xr-launcher.sh"
echo "installed the launcher to $DEST"
[ -x "$DEST/moonlight-xr" ] || echo "note: $DEST/moonlight-xr is not there yet; the launcher streams flat (SDL) until the VR client is installed next to it"
