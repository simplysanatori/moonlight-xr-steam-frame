#!/bin/bash
# Moonlight XR Native: configure, build and audit on the Steam Frame. GPLv3.
#   ./build.sh [target...]      default: all
# The SteamOS image on the Frame ships clang 19, cmake, ninja, glslc and the
# -dev headers, so the build runs on the host and links only host libraries.
set -euo pipefail
cd "$(dirname "$0")"
for t in clang clang++ cmake ninja glslc pkg-config; do
    command -v "$t" >/dev/null || { echo "missing host tool: $t (did a SteamOS update drop it?)"; exit 1; }
done
git submodule update --init --recursive
BUILD="${BUILD:-build}"
[ -f "$BUILD/build.ninja" ] || cmake -S . -B "$BUILD" -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
nice -n 5 ninja -C "$BUILD" -j"${JOBS:-3}" "$@"
for bin in "$BUILD/hello/mlxr-hello" "$BUILD/moonlight-xr"; do
    [ -x "$bin" ] && ./tools/ldd-audit.sh "$bin"
done
