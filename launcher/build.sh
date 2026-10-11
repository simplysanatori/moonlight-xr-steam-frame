#!/bin/bash
# Moonlight XR Native launcher: builds the patched moonlight-qt 6.1.0 on the host. GPLv3.
#   launcher/build.sh            -> build-launcher/moonlight-xr-launcher
#   JOBS=3 (default)  BUILD_DIR=build-launcher
# The submodule third_party/moonlight-qt is never modified: its tree is copied to
# build-launcher/src, launcher/src/* (new files) and launcher/qml/*.qml (new pages, into app/gui)
# are copied in and launcher/patches/*.patch applied
# there. The copy is refreshed only when the submodule commit, the patches or launcher/src change.
# SteamOS ships what this needs: qt6-base/declarative/svg/tools, SDL2, SDL2_ttf, FFmpeg, opus,
# OpenSSL, libva, libdrm.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BUILD="${BUILD_DIR:-build-launcher}"
JOBS="${JOBS:-3}"
SUB=third_party/moonlight-qt

for t in qmake6 make g++ gcc rsync patch pkg-config; do
    command -v "$t" >/dev/null || { echo "missing host tool: $t"; exit 1; }
done
for p in sdl2 SDL2_ttf opus libavcodec openssl libva libdrm; do
    pkg-config --exists "$p" || { echo "missing host library (pkg-config $p); did a SteamOS update drop it?"; exit 1; }
done

if [ ! -e "$SUB/moonlight-qt.pro" ] || [ ! -e "$SUB/moonlight-common-c/moonlight-common-c/src/Limelight.h" ]; then
    git submodule update --init --recursive "$SUB"
fi
SUBREV="$(git -C "$SUB" rev-parse HEAD)"
[ "$SUBREV" = "f786e94c7b2f943e24e65d7d74deb539b827fc84" ] || echo "note: $SUB is at $SUBREV, not v6.1.0; the patches may not apply"

mkdir -p "$BUILD"
STAMP="$(cat launcher/patches/*.patch launcher/src/* launcher/qml/* assets/branding/icon-256.png VERSION | sha256sum | cut -c1-16)-$SUBREV"
if [ "$(cat "$BUILD/.stamp" 2>/dev/null)" != "$STAMP" ]; then
    echo "== preparing $BUILD/src (moonlight-qt $SUBREV + launcher/src + patches)"
    rm -rf "$BUILD/src"
    mkdir -p "$BUILD/src"
    rsync -a --exclude='.git*' --exclude=/libs "$SUB/" "$BUILD/src/"
    cp launcher/src/* "$BUILD/src/app/streaming/"
    cp launcher/qml/*.qml "$BUILD/src/app/gui/"   # new pages (lane artemis-settings: XrSettingsView.qml)
    cp assets/branding/icon-256.png "$BUILD/src/app/res/moonlight-xr.png"   # window icon (patch 0006)
    # the release version for the Settings page footer (patch 0005)
    printf '#pragma once\n#define MLXR_VERSION "%s"\n' "$(head -n 1 VERSION)" > "$BUILD/src/app/streaming/mlxrversion.h"
    for p in launcher/patches/*.patch; do
        echo "   applying $(basename "$p")"
        if ! patch -d "$BUILD/src" -p1 --no-backup-if-mismatch -s < "$p"; then
            echo "patch does not apply: $p"
            echo "The submodule changed? See launcher/patches.sh (workdir / export) to rebase the patches."
            rm -rf "$BUILD/src"
            exit 1
        fi
    done
    echo "$STAMP" > "$BUILD/.stamp"
fi

mkdir -p "$BUILD/obj"
cd "$BUILD/obj"
[ -f Makefile ] || qmake6 ../src/moonlight-qt.pro CONFIG+=release
nice -n 10 make -j"$JOBS"
cd "$ROOT"
install -m755 "$BUILD/obj/app/moonlight" "$BUILD/moonlight-xr-launcher"
echo "built $BUILD/moonlight-xr-launcher"
./tools/ldd-audit.sh "$BUILD/moonlight-xr-launcher"
