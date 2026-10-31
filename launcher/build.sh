#!/bin/bash
# Moonlight XR Native launcher: builds the patched moonlight-qt 6.1.0 on the host. GPLv3.
#   launcher/build.sh            -> build-launcher/moonlight-xr-launcher
#   JOBS=3 (default)  BUILD_DIR=build-launcher
#   SKIP_AUDIT=1                 skip the ldd audit (it needs pacman, so it is
#                                skipped by itself on non-SteamOS hosts)
# The submodule third_party/moonlight-qt is never modified: its tree is copied to
# build-launcher/src, launcher/src/* (new files) and launcher/qml/*.qml (new pages, into app/gui)
# are copied in and launcher/patches/*.patch applied
# there. The copy is refreshed only when the submodule commit, the patches or launcher/src change.
# SteamOS ships what this needs: qt6-base/declarative/svg/tools, SDL2, SDL2_ttf, opus, OpenSSL,
# libva, libdrm. FFmpeg (libavcodec, libavutil, libswscale) is the app's bundled 7.0.2 when it is built
# (tools/build-bundled-libs.sh; build.sh builds it and passes MLXR_BUNDLED_LIBS_DIR): the launcher
# then finds it in app/lib through its RUNPATH $ORIGIN/lib, as the VR client does, and is built
# without libplacebo (its soname changes with every release; in hand-off mode the launcher draws no
# video). NO_BUNDLE=1 links the system's FFmpeg (not for a release).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BUILD="${BUILD_DIR:-build-launcher}"
JOBS="${JOBS:-3}"
SUB=third_party/moonlight-qt

for t in qmake6 make g++ gcc rsync patch pkg-config; do
    command -v "$t" >/dev/null || { echo "missing host tool: $t"; exit 1; }
done
BUNDLE="${MLXR_BUNDLED_LIBS_DIR:-${MLXR_BUNDLE_ROOT:-$HOME/Games/MoonlightXRNative/third-party-build/bundled}/dist}"
QMAKE_ARGS=(CONFIG+=release)
if [ "${NO_BUNDLE:-0}" != 1 ] && [ -f "$BUNDLE/lib/pkgconfig/libavcodec.pc" ]; then
    export PKG_CONFIG_PATH="$BUNDLE/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
    # mlxr_bundled: the RUNPATH $ORIGIN/lib line appended to app.pro below. It is not passed here because
    # moonlight-qt.pro is a subdirs project: its Makefile hands the arguments to the app's qmake through
    # make, which eats the $ of $ORIGIN (1.1.1's first CI run linked -rpath,$ORIGIN/RIGIN/lib).
    QMAKE_ARGS+=(CONFIG+=disable-libplacebo CONFIG+=mlxr_bundled)
    echo "FFmpeg: bundled ($BUNDLE)"
else
    echo "FFmpeg: the system's (no bundle at $BUNDLE, or NO_BUNDLE=1): not for a release"
fi
for p in sdl2 SDL2_ttf opus libavcodec openssl libva libdrm; do
    pkg-config --exists "$p" || { echo "missing host library (pkg-config $p); did a SteamOS update drop it?"; exit 1; }
done

if [ ! -e "$SUB/moonlight-qt.pro" ] || [ ! -e "$SUB/moonlight-common-c/moonlight-common-c/src/Limelight.h" ]; then
    git submodule update --init --recursive "$SUB"
fi
SUBREV="$(git -C "$SUB" rev-parse HEAD)"
[ "$SUBREV" = "f786e94c7b2f943e24e65d7d74deb539b827fc84" ] || echo "note: $SUB is at $SUBREV, not v6.1.0; the patches may not apply"

# the binding core the VR client reads bindings.ini with, compiled into the launcher (launcher/src/mlxrbindings.h)
BINDCORE="xr/core/xr_bind.c xr/core/xr_bind.h xr/core/xr_gamepad.c xr/core/xr_gamepad.h xr/core/xr_shared.h"

mkdir -p "$BUILD"
STAMP="$(cat launcher/build.sh launcher/patches/*.patch launcher/src/* launcher/qml/* $BINDCORE assets/branding/icon-256.png VERSION | sha256sum | cut -c1-16)-$SUBREV"
if [ "$(cat "$BUILD/.stamp" 2>/dev/null)" != "$STAMP" ]; then
    echo "== preparing $BUILD/src (moonlight-qt $SUBREV + launcher/src + patches)"
    rm -rf "$BUILD/src"
    mkdir -p "$BUILD/src"
    rsync -a --exclude='.git*' --exclude=/libs "$SUB/" "$BUILD/src/"
    cp launcher/src/* "$BUILD/src/app/streaming/"
    cp $BINDCORE "$BUILD/src/app/streaming/"   # lane wave2-bind-editor: xr_bind.c is pure C, so there is no second parser
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
    # with the bundled FFmpeg the launcher finds it in lib/ beside itself, as the VR client does.
    # In a .pro file \$\$ is a literal $$, which make turns into the $ the linker needs.
    printf '%s\n' '' '# Moonlight XR (launcher/build.sh): the bundled FFmpeg in lib/ beside the binary' \
        "mlxr_bundled: QMAKE_LFLAGS += -Wl,--enable-new-dtags -Wl,-rpath,\\'\\\$\\\$ORIGIN/lib\\'" >> "$BUILD/src/app/app.pro"
    echo "$STAMP" > "$BUILD/.stamp"
fi

# a different FFmpeg (bundled or not, or another bundle) means a fresh qmake and a full rebuild
QSTAMP="$(printf '%s\n' "${QMAKE_ARGS[@]}" "$PKG_CONFIG_PATH" | sha256sum | cut -c1-16)"
[ "$(cat "$BUILD/obj/.qmake-stamp" 2>/dev/null)" = "$QSTAMP" ] || rm -rf "$BUILD/obj"
mkdir -p "$BUILD/obj"
cd "$BUILD/obj"
[ -f Makefile ] || { qmake6 ../src/moonlight-qt.pro "${QMAKE_ARGS[@]}" && echo "$QSTAMP" > .qmake-stamp; }
nice -n 10 make -j"$JOBS"
cd "$ROOT"
install -m755 "$BUILD/obj/app/moonlight" "$BUILD/moonlight-xr-launcher"
# a lib/ beside it, as in app/, so it runs from here with the bundled FFmpeg
if [ -n "${PKG_CONFIG_PATH:-}" ] && [ "${PKG_CONFIG_PATH%%:*}" = "$BUNDLE/lib/pkgconfig" ]; then ln -sfn "$BUNDLE/lib" "$BUILD/lib"
else rm -f "$BUILD/lib"; fi
echo "built $BUILD/moonlight-xr-launcher"
if [ "${SKIP_AUDIT:-0}" = 1 ]; then
    echo "SKIP_AUDIT=1: ldd audit skipped"
elif ! command -v pacman >/dev/null; then
    echo "no pacman on this host: ldd audit skipped (it checks libraries against SteamOS packages)"
else
    ./tools/ldd-audit.sh "$BUILD/moonlight-xr-launcher"
fi
