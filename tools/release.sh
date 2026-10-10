#!/bin/bash
# Builds the release tarball and checks it. GPLv3.
#   tools/release.sh
# Writes moonlight-xr-native-<version>-linux-arm64.tar.gz and SHA256SUMS into
# $OUT (default ~/Games/MoonlightXRNative/releases). The tarball unpacks to
# MoonlightXRNative/ holding install.sh, steam_cef.py, the licence files and
# app/ (binaries, launcher, assets, the Moonlight Qt import script).
# The version comes from `git describe`; tag a release (git tag v0.1.0) first.
#
# Expects a finished build (./build.sh). Unless NO_VERIFY=1 it then unpacks the
# tarball into a temporary directory, runs its install.sh there (NO_SHORTCUT=1,
# a temporary DEST and config dir), compares every installed file with the
# tarball, and runs the ldd audit on the unpacked binaries (skipped on hosts
# without pacman).
# Environment: BUILD (default build), OUT, NO_VERIFY.
set -euo pipefail
cd "$(dirname "$0")/.."
BUILD="${BUILD:-build}"
OUT="${OUT:-$HOME/Games/MoonlightXRNative/releases}"
for b in "$BUILD/moonlight-xr" "$BUILD/backend/mlxr-cli"; do
    [ -x "$b" ] || { echo "missing $b: run ./build.sh first"; exit 1; }
done
if VER=$(git describe --tags --dirty 2>/dev/null); then :
else VER="0.0.0-$(git rev-list --count HEAD)-g$(git rev-parse --short HEAD)$(git diff --quiet HEAD -- || echo -dirty)"; fi
VER="${VER#v}"
case "$VER" in *-dirty) echo "warning: working tree has uncommitted changes ($VER)";; esac
NAME="moonlight-xr-native-$VER-linux-arm64"
mkdir -p "$OUT"; OUT="$(cd "$OUT" && pwd)"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
ROOT="$TMP/stage/MoonlightXRNative"
mkdir -p "$ROOT/app"

strip_to() {   # copy a binary without debug info when a strip tool exists
    if command -v strip >/dev/null; then strip --strip-unneeded -o "$2" "$1"; else install -m755 "$1" "$2"; fi
    chmod 755 "$2"
}
strip_to "$BUILD/moonlight-xr" "$ROOT/app/moonlight-xr"
strip_to "$BUILD/backend/mlxr-cli" "$ROOT/app/mlxr-cli"
install -m755 packaging/moonlight-xr.sh "$ROOT/app/moonlight-xr.sh"
install -m755 backend/seed-from-moonlight-qt.sh "$ROOT/app/seed-from-moonlight-qt.sh"
install -m644 backend/seed_moonlight_qt.py "$ROOT/app/seed_moonlight_qt.py"
cp -a assets "$ROOT/app/assets"
install -m755 packaging/install.sh "$ROOT/install.sh"
install -m644 packaging/steam_cef.py "$ROOT/steam_cef.py"
[ -d packaging/art ] && cp -a packaging/art "$ROOT/art"
for f in README.md LICENSE.txt CREDITS.md; do install -m644 "$f" "$ROOT/$f"; done
echo "$VER" > "$ROOT/VERSION"

EPOCH="$(git log -1 --format=%ct)"
( cd "$TMP/stage" && tar --sort=name --owner=0 --group=0 --numeric-owner --mtime="@$EPOCH" -cf - MoonlightXRNative ) \
    | gzip -9n > "$OUT/$NAME.tar.gz"
( cd "$OUT" && { grep -v " $NAME.tar.gz\$" SHA256SUMS 2>/dev/null || true; sha256sum "$NAME.tar.gz"; } > SHA256SUMS.new && mv SHA256SUMS.new SHA256SUMS )
echo "built $OUT/$NAME.tar.gz ($(du -h "$OUT/$NAME.tar.gz" | cut -f1))"
cat "$OUT/SHA256SUMS"

[ "${NO_VERIFY:-0}" = 1 ] && exit 0
echo "== verify: unpack, install into a temporary prefix, audit"
( cd "$OUT" && sha256sum -c --ignore-missing SHA256SUMS )
mkdir -p "$TMP/unpack" "$TMP/home"
tar -xzf "$OUT/$NAME.tar.gz" -C "$TMP/unpack"
U="$TMP/unpack/MoonlightXRNative"
[ "$(cat "$U/VERSION")" = "$VER" ] || { echo "VERSION mismatch"; exit 1; }
# twice: the second run must change nothing and still succeed
for i in 1 2; do
    DEST="$TMP/inst/app" NO_SHORTCUT=1 MLXR_CONFIG_DIR="$TMP/cfg" "$U/install.sh" > "$TMP/install.$i.log"
done
tail -n 4 "$TMP/install.2.log"
diff -r "$U/app" "$TMP/inst/app" -x LICENSE.txt -x CREDITS.md >/dev/null || { echo "installed tree differs from the tarball"; diff -rq "$U/app" "$TMP/inst/app" | head; exit 1; }
for f in LICENSE.txt CREDITS.md; do cmp "$U/$f" "$TMP/inst/app/$f"; done
MLXR_CONFIG_DIR="$TMP/cfg" "$TMP/inst/app/mlxr-cli" 2>&1 | head -n 3 || true
if command -v pacman >/dev/null; then
    ./tools/ldd-audit.sh "$TMP/inst/app/moonlight-xr"
    ./tools/ldd-audit.sh "$TMP/inst/app/mlxr-cli"
else
    echo "no pacman: ldd audit skipped"
fi
echo "release ok: $OUT/$NAME.tar.gz"
