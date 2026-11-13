#!/bin/bash
# Dev build of the XR client for THIS Steam Frame, cross-compiled on the desktop (never on the Frame).
#   tools/desktop-dev-build.sh            sync source, build, run ctest under qemu, refresh app-dev
#   NO_TEST=1 ...                         skip ctest
#   SYNC_SYSROOT=1 ...                    re-copy the Frame's /usr, LiteRT and bundled libs first
#   DEV_DIR=mlxr-x ...                    build in ~/frame-build/mlxr-x on the desktop instead (a second
#                                         tree or worktree side by side), sharing mlxr's sysroot read-only
#   NO_INSTALL=1 ...                      leave app-dev alone (the binaries stay on the desktop)
# The source is the checkout or worktree this script is in.
# Desktop side: ~/frame-build/mlxr/{sysroot,src,build,ccache}; takes ~/frame-build/build.lock.
# The Qt launcher is not rebuilt: app-dev keeps the released launcher. GPLv3.
set -euo pipefail
SRC="$(cd "$(dirname "$0")/.." && pwd)"
ROOT_DIR="$(dirname "$SRC")"
APPDEV="${APPDEV:-$ROOT_DIR/app-dev}"
DEV_DIR="${DEV_DIR:-mlxr}"
D=desktop; R='$HOME/frame-build/mlxr'; B='$HOME/frame-build/'"$DEV_DIR"

if [ "$DEV_DIR" != mlxr ] && [ "${SYNC_SYSROOT:-0}" = 1 ]; then
    echo "SYNC_SYSROOT only with the default DEV_DIR (the sysroot is mlxr's)"; exit 1
fi
if [ "${SYNC_SYSROOT:-0}" = 1 ] || ! ssh $D "test -d $R/sysroot/usr/include"; then
    echo "== syncing the Frame's /usr into the desktop sysroot"
    ssh $D "mkdir -p $R/sysroot/usr/share $R/sysroot/mlxr-tp/litert $R/sysroot/mlxr-tp/bundled"
    (cd / && nice rsync -a --copy-unsafe-links -e ssh \
      --exclude='/lib/python*' --exclude='/lib/modules' --exclude='/lib/firmware' --exclude='/lib/perl*' \
      --exclude='/lib/locale' --exclude='/lib/qt6' --exclude='/lib/libreoffice' --exclude='/lib/steam*' \
      --exclude='/lib/node_modules' --exclude='/lib/go*' --exclude='/lib/clang' --exclude='/lib/llvm*' --exclude='/lib/cmake' \
      usr/include usr/lib $D:'frame-build/mlxr/sysroot/usr/') || [ $? = 23 ]
    (cd /usr/share && nice rsync -a --copy-unsafe-links -e ssh pkgconfig wayland wayland-protocols vulkan $D:'frame-build/mlxr/sysroot/usr/share/') || [ $? = 23 ]
    ssh $D "cd $R/sysroot && ln -sfn usr/lib lib && ln -sfn usr/lib lib64 && ln -sfn lib usr/lib64"
    TP="$ROOT_DIR/third-party-build"
    for d in litert bundled; do nice rsync -a --delete -e ssh "$TP/$d/dist" $D:"frame-build/mlxr/sysroot/mlxr-tp/$d/"; done
    # the bundled .pc files point into the sysroot copy
    ssh $D 'cd $HOME/frame-build/mlxr/sysroot/mlxr-tp/bundled/dist/lib/pkgconfig && sed -i "s#/home/steamos/Games/MoonlightXRNative/third-party-build#$HOME/frame-build/mlxr/sysroot/mlxr-tp#" *.pc'
fi

echo "== syncing source (HEAD $(git -C "$SRC" rev-parse --short HEAD), $(git -C "$SRC" status --porcelain -uno | wc -l) dirty paths)"
ssh $D "mkdir -p $B/src $B/out"
nice rsync -a --delete -e ssh --exclude=/.git --exclude='/build*' --exclude=/third_party/moonlight-qt \
    --exclude='/assets/models/frame_controller_*' "$SRC/" $D:"frame-build/$DEV_DIR/src/"

echo "== building on the desktop"
ssh $D "bash -s" "${NO_TEST:-0}" "$DEV_DIR" <<'REMOTE'
set -euo pipefail
export MLXR_DEV_ROOT=$HOME/frame-build/$2
# the one sysroot, mlxr's, read by every DEV_DIR (desktop-dev.cmake)
export MLXR_DEV_SYSROOT=$HOME/frame-build/mlxr/sysroot
R=$MLXR_DEV_ROOT; S=$MLXR_DEV_SYSROOT/mlxr-tp
export CCACHE_DIR=$R/ccache CCACHE_BASEDIR=$R
exec 9>$HOME/frame-build/build.lock
flock 9
cd $R
mkdir -p bin
for c in clang clang++; do printf '#!/bin/sh\nexec /usr/bin/%s "$@" -Wno-error\n' $c > bin/$c; chmod +x bin/$c; done
cmake -S src -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=$R/src/tools/desktop-dev.cmake \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  -DMLXR_LITERT_DIR=$S/litert/dist -DMLXR_BUNDLED_LIBS_DIR=$S/bundled/dist >build-configure.log 2>&1 || { tail -40 build-configure.log; exit 1; }
nice -n 5 ninja -C build -j16 2>&1 | tail -25
if [ "$1" != 1 ]; then
  echo "== ctest (under qemu-aarch64; Vulkan tests skip)"
  rm -f ctest.log
  ( cd build && QEMU_LD_PREFIX=$MLXR_DEV_SYSROOT MLXR_SOFT_GPU=1 ctest --output-on-failure -j8 -E '^(audio_pulse|video_)' ) >ctest.log 2>&1 || true
  tail -40 ctest.log
fi
REMOTE

if [ "${NO_INSTALL:-0}" = 1 ]; then
    echo "done: frame-build/$DEV_DIR/build on the desktop (NO_INSTALL=1, $APPDEV left alone)"; exit 0
fi
echo "== refreshing $APPDEV"
[ -d "$APPDEV" ] || { echo "no $APPDEV (create it first, see DEV-BUILD.txt notes)"; exit 1; }
for b in moonlight-xr backend/mlxr-cli; do
    rsync -e ssh $D:"frame-build/$DEV_DIR/build/$b" "$APPDEV/$(basename "$b").new"
    mv -f "$APPDEV/$(basename "$b").new" "$APPDEV/$(basename "$b")"; chmod 755 "$APPDEV/$(basename "$b")"
done
printf 'Moonlight XR DEV BUILD\nbranch: %s\ncommit: %s%s\nbuilt:  %s (desktop cross build, tools/desktop-dev-build.sh)\nBase: released %s app/ with moonlight-xr and mlxr-cli swapped.\n' \
    "$(git -C "$SRC" branch --show-current)" "$(git -C "$SRC" rev-parse --short HEAD)" \
    "$([ -n "$(git -C "$SRC" status --porcelain -uno)" ] && echo ' + uncommitted changes')" "$(date -Is)" "$(cat "$ROOT_DIR/app/VERSION")" > "$APPDEV/DEV-BUILD.txt"
echo "done: $APPDEV"
