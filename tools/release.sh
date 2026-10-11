#!/bin/bash
# Builds the release tarball and checks it. GPLv3.
#   tools/release.sh
# Writes moonlight-xr-native-<version>-linux-arm64.tar.gz and SHA256SUMS into
# $OUT (default ~/Games/MoonlightXRNative/releases). The tarball unpacks to
# MoonlightXRNative/ holding install.sh, steam_cef.py, dock_icon.py, README.md, docs/ (user guide,
# changelog, building, privacy audit), the licence files and
# app/ (binaries, launcher, assets, the Moonlight Qt import script) and the
# Frame controller model converter. The converted Steam Frame controller
# models (Valve's) are never in the tarball: install.sh makes them on the target.
# The version is the top-level VERSION file (e.g. 0.6.0-alpha.1, tag v0.6.0-alpha.1).
# When HEAD carries a v* tag it must match VERSION.
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
VER="$(head -n 1 VERSION)"
[ -n "$VER" ] || { echo "VERSION is empty"; exit 1; }
if TAG=$(git describe --tags --exact-match --match 'v*' HEAD 2>/dev/null) && [ "$TAG" != "v$VER" ]; then
    echo "HEAD is tagged $TAG but VERSION says $VER"; exit 1
fi
git diff --quiet HEAD -- || { VER="$VER-dirty"; echo "warning: working tree has uncommitted changes ($VER)"; }
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
# The flat Moonlight UI (patched moonlight-qt), when built (launcher/build.sh)
if [ -x build-launcher/moonlight-xr-launcher ]; then
    strip_to build-launcher/moonlight-xr-launcher "$ROOT/app/moonlight-xr-launcher"
    install -m755 launcher/moonlight-xr-launcher.sh "$ROOT/app/moonlight-xr-launcher.sh"
else
    echo "warning: no build-launcher/moonlight-xr-launcher: the release opens the VR client directly"
fi
install -m755 backend/seed-from-moonlight-qt.sh "$ROOT/app/seed-from-moonlight-qt.sh"
install -m644 backend/seed_moonlight_qt.py "$ROOT/app/seed_moonlight_qt.py"
install -m755 tools/bug-report.sh "$ROOT/app/bug-report.sh"
install -m755 tools/vrtiming.py "$ROOT/app/vrtiming.py"
mkdir -p "$ROOT/app/docs/probe" && install -m644 docs/probe/SUMMARY.md "$ROOT/app/docs/probe/SUMMARY.md"
# tracked files only: nothing generated or dropped into assets/ locally gets in
git ls-files -z assets | while IFS= read -r -d '' f; do install -D -m644 "$f" "$ROOT/app/$f"; done
# Valve's models, converted: made by install.sh on the user's machine, never shipped
rm -f "$ROOT"/app/assets/models/frame_controller_*
# the converter itself is ours; install.sh runs it against the SteamVR on the target
install -m755 tools/frame-models/frame-controller-models.py "$ROOT/frame-controller-models.py"
install -m755 packaging/install.sh "$ROOT/install.sh"
install -m644 packaging/steam_cef.py "$ROOT/steam_cef.py"
install -m644 packaging/dock_icon.py "$ROOT/dock_icon.py"
[ -d packaging/art ] && cp -a packaging/art "$ROOT/art"
for f in README.md LICENSE.txt CREDITS.md; do install -m644 "$f" "$ROOT/$f"; done
mkdir -p "$ROOT/docs"
for f in USER_GUIDE.md CHANGELOG.md BUILDING.md PRIVACY_AUDIT.md; do [ -f "docs/$f" ] && install -m644 "docs/$f" "$ROOT/docs/$f"; done
[ -f "docs/RELEASE_NOTES_v$VER.md" ] && install -m644 "docs/RELEASE_NOTES_v$VER.md" "$ROOT/docs/"
[ -z "$(find "$ROOT" -name 'frame_controller_*' -print -quit)" ] || { echo "a Steam Frame controller model got into the release"; exit 1; }
# nothing private or machine-specific: config, pairing, keys, logs, reports, the LiteRT build
bad="$(find "$ROOT" \( -name 'config.ini' -o -name 'hosts.ini' -o -name 'uniqueid' -o -name '*.key' -o -name '*.pem' \
    -o -name 'servercerts' -o -name '*.log' -o -name '*.log.[0-9]' -o -name 'mlxr-report-*' -o -name '*.conf' \
    -o -name 'libmlxr_litert*' -o -name '*.a' -o -name '*.bak*' \) -print)"
[ -z "$bad" ] || { echo "private or build files would be shipped:"; echo "$bad"; exit 1; }
echo "$VER" > "$ROOT/VERSION"; cp "$ROOT/VERSION" "$ROOT/app/VERSION"

EPOCH="$(git log -1 --format=%ct)"
( cd "$TMP/stage" && tar --sort=name --owner=0 --group=0 --numeric-owner --mtime="@$EPOCH" -cf - MoonlightXRNative ) \
    | gzip -9n > "$OUT/$NAME.tar.gz"
( cd "$OUT" && { grep -v " $NAME.tar.gz\$" SHA256SUMS 2>/dev/null || true; sha256sum "$NAME.tar.gz"; } > SHA256SUMS.new && mv SHA256SUMS.new SHA256SUMS )
echo "built $OUT/$NAME.tar.gz ($(du -h "$OUT/$NAME.tar.gz" | cut -f1))"
cat "$OUT/SHA256SUMS"

# personal data (tools/privacy-check.sh; your own values come from its local pattern list)
./tools/privacy-check.sh "$OUT/$NAME.tar.gz" || { echo "privacy check failed: $OUT/$NAME.tar.gz must not be published"; exit 1; }

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
# (install.sh makes the Frame controller models from the local SteamVR, so the install may have them and the tarball never does)
[ -z "$(find "$U" -name 'frame_controller_*' -print -quit)" ] || { echo "the tarball holds a Steam Frame controller model"; exit 1; }
diff -r "$U/app" "$TMP/inst/app" -x LICENSE.txt -x CREDITS.md -x 'frame_controller_*' >/dev/null || { echo "installed tree differs from the tarball"; diff -rq "$U/app" "$TMP/inst/app" -x 'frame_controller_*' | head; exit 1; }
for f in LICENSE.txt CREDITS.md; do cmp "$U/$f" "$TMP/inst/app/$f"; done
MLXR_CONFIG_DIR="$TMP/cfg" "$TMP/inst/app/mlxr-cli" 2>&1 | head -n 3 || true
if command -v pacman >/dev/null; then
    ./tools/ldd-audit.sh "$TMP/inst/app/moonlight-xr"
    ./tools/ldd-audit.sh "$TMP/inst/app/mlxr-cli"
else
    echo "no pacman: ldd audit skipped"
fi
echo "release ok: $OUT/$NAME.tar.gz"
