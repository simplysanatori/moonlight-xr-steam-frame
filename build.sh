#!/bin/bash
# Moonlight XR Native: configure, build and audit on the Steam Frame. GPLv3.
#   ./build.sh [target...]      default: all
#
# The SteamOS image on the Frame ships clang 19, cmake, ninja, glslc and the
# -dev headers, so the build runs on the host and links only host libraries.
#
# Environment:
#   BUILD=build           build directory
#   JOBS=3                parallel jobs
#   NO_DEPTH=1            do not build LiteRT; the depth targets are skipped
#                         (the app then has no 3D depth)
#   MLXR_LITERT_ROOT=...  where depth/tools/build-litert.sh puts LiteRT
#                         (default ~/Games/MoonlightXRNative/third-party-build/litert)
#   SKIP_AUDIT=1          skip the ldd audit (it needs pacman, so it is
#                         skipped by itself on non-SteamOS hosts)
set -euo pipefail
cd "$(dirname "$0")"
for t in clang clang++ cmake ninja glslc pkg-config; do
    command -v "$t" >/dev/null || { echo "missing host tool: $t (did a SteamOS update drop it?)"; exit 1; }
done
git submodule update --init --recursive

# LiteRT (depth model runtime) is built outside the repository, once per
# machine: about 12 minutes, and the first run fetches sources from GitHub.
LITERT_ROOT="${MLXR_LITERT_ROOT:-$HOME/Games/MoonlightXRNative/third-party-build/litert}"
cmake_args=()
if [ "${NO_DEPTH:-0}" = 1 ]; then
    echo "NO_DEPTH=1: not building LiteRT; depth targets are skipped if it is absent"
elif [ ! -f "$LITERT_ROOT/dist/lib/libmlxr_litert.a" ]; then
    echo "LiteRT not found in $LITERT_ROOT/dist: building it (about 12 minutes; NO_DEPTH=1 skips this)"
    MLXR_LITERT_ROOT="$LITERT_ROOT" JOBS="${JOBS:-3}" ./depth/tools/build-litert.sh
fi
[ -f "$LITERT_ROOT/dist/lib/libmlxr_litert.a" ] && cmake_args+=("-DMLXR_LITERT_DIR=$LITERT_ROOT/dist")

BUILD="${BUILD:-build}"
# Configure every time: it is quick, and it notices a LiteRT that appeared
# since the last run.
cmake -S . -B "$BUILD" -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ "${cmake_args[@]}"
nice -n 5 ninja -C "$BUILD" -j"${JOBS:-3}" "$@"

# The flat Moonlight UI (patched moonlight-qt 6.1.0): full builds only, NO_LAUNCHER=1 skips it
if [ $# -eq 0 ] && [ "${NO_LAUNCHER:-0}" != 1 ] && [ -x launcher/build.sh ]; then
    ./launcher/build.sh
fi

if [ "${SKIP_AUDIT:-0}" = 1 ]; then
    echo "SKIP_AUDIT=1: ldd audit skipped"
elif ! command -v pacman >/dev/null; then
    echo "no pacman on this host: ldd audit skipped (it checks libraries against SteamOS packages)"
else
    # With no target named, both shipped binaries must exist and pass.
    for bin in "$BUILD/moonlight-xr" "$BUILD/backend/mlxr-cli" build-launcher/moonlight-xr-launcher; do
        [ "$bin" = build-launcher/moonlight-xr-launcher ] && [ ! -x "$bin" ] && continue
        if [ -x "$bin" ]; then ./tools/ldd-audit.sh "$bin"
        elif [ $# -eq 0 ]; then echo "expected $bin after a full build"; exit 1; fi
    done
fi
