#!/bin/bash
# Builds the release tarball and zip and checks them. GPLv3.
#   tools/release.sh
# Writes moonlight-xr-steam-frame-<version>-linux-arm64.tar.gz, the same tree as
# moonlight-xr-steam-frame-<version>-linux-arm64.zip (same files, same modes; for the community
# installers Frametanium and FrameDrop, see docs/dev/reports/packaging.md) and SHA256SUMS into
# $OUT (default ~/Games/MoonlightXRNative/releases). Both archives unpack to
# MoonlightXRNative/ holding install.sh, steam_cef.py, dock_icon.py, README.md, docs/ (user guide,
# changelog, building, privacy audit), the licence files and
# app/ (binaries incl. mlxr-gamepad-probe, launcher, assets, the Moonlight Qt import script) and the
# Frame controller model converter. The converted Steam Frame controller
# models (Valve's) are never in the tarball: install.sh makes them on the target.
# The version is the top-level VERSION file (e.g. 1.0.0, tag v1.0.0).
# When HEAD carries a v* tag it must match VERSION.
#
# Expects a finished build (./build.sh). Unless NO_VERIFY=1 it then unpacks the
# tarball (and the zip, which must give the same tree) into a temporary directory, runs its install.sh there (NO_SHORTCUT=1,
# a temporary DEST and config dir), compares every installed file with the
# tarball, and runs the ldd audit on the unpacked binaries (skipped on hosts
# without pacman).
# Environment: BUILD (default build), OUT, NO_VERIFY.
set -euo pipefail
cd "$(dirname "$0")/.."
BUILD="${BUILD:-build}"
OUT="${OUT:-$HOME/Games/MoonlightXRNative/releases}"
for b in "$BUILD/moonlight-xr" "$BUILD/backend/mlxr-cli" "$BUILD/input/mlxr-gamepad-probe"; do
    [ -x "$b" ] || { echo "missing $b: run ./build.sh first"; exit 1; }
done
VER="$(head -n 1 VERSION)"
[ -n "$VER" ] || { echo "VERSION is empty"; exit 1; }
if TAG=$(git describe --tags --exact-match --match 'v*' HEAD 2>/dev/null) && [ "$TAG" != "v$VER" ]; then
    echo "HEAD is tagged $TAG but VERSION says $VER"; exit 1
fi
git diff --quiet HEAD -- || { VER="$VER-dirty"; echo "warning: working tree has uncommitted changes ($VER)"; }
NAME="moonlight-xr-steam-frame-$VER-linux-arm64"
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
strip_to "$BUILD/input/mlxr-gamepad-probe" "$ROOT/app/mlxr-gamepad-probe"
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
# The same staged tree as a zip. Python's zipfile (the Frame has no promise of a zip tool): sorted names,
# commit time, and the Unix mode of every entry in external_attr, which is what keeps install.sh and the
# binaries executable when Frametanium, FrameDrop or unzip unpack it.
rm -f "$OUT/$NAME.zip"
python3 -I - "$TMP/stage" "$OUT/$NAME.zip" "$EPOCH" <<'PY'
import os, stat, sys, time, zipfile
stage, out, epoch = sys.argv[1], sys.argv[2], int(sys.argv[3])
when = time.gmtime(max(epoch, 315532800))[:6]          # a zip cannot date before 1980
names = []
for dirpath, dirs, files in os.walk(stage):
    dirs.sort()
    for n in dirs + files:
        names.append(os.path.relpath(os.path.join(dirpath, n), stage))
names.sort()
with zipfile.ZipFile(out + ".part", "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for rel in names:
        path = os.path.join(stage, rel)
        st = os.lstat(path)
        mode = stat.S_IMODE(st.st_mode)
        if stat.S_ISDIR(st.st_mode):
            info = zipfile.ZipInfo(rel + "/", when)
            info.external_attr = ((stat.S_IFDIR | mode) << 16) | 0x10
            z.writestr(info, b"")
        elif stat.S_ISLNK(st.st_mode):
            info = zipfile.ZipInfo(rel, when)
            info.external_attr = (stat.S_IFLNK | 0o777) << 16
            z.writestr(info, os.readlink(path))
        else:
            info = zipfile.ZipInfo(rel, when)
            info.external_attr = (stat.S_IFREG | mode) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            with open(path, "rb") as f:
                z.writestr(info, f.read(), compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
os.replace(out + ".part", out)
PY
( cd "$OUT" && { grep -v -e " $NAME.tar.gz\$" -e " $NAME.zip\$" SHA256SUMS 2>/dev/null || true; sha256sum "$NAME.tar.gz" "$NAME.zip"; } > SHA256SUMS.new && mv SHA256SUMS.new SHA256SUMS )
echo "built $OUT/$NAME.tar.gz ($(du -h "$OUT/$NAME.tar.gz" | cut -f1)) and $NAME.zip ($(du -h "$OUT/$NAME.zip" | cut -f1))"
cat "$OUT/SHA256SUMS"

# personal data (tools/privacy-check.sh; your own values come from its local pattern list). It reads a
# tarball or a directory, so the zip is checked unpacked.
./tools/privacy-check.sh "$OUT/$NAME.tar.gz" || { echo "privacy check failed: $OUT/$NAME.tar.gz must not be published"; exit 1; }
python3 -I -c 'import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])' "$OUT/$NAME.zip" "$TMP/zipcheck"
./tools/privacy-check.sh "$TMP/zipcheck" || { echo "privacy check failed: $OUT/$NAME.zip must not be published"; exit 1; }

[ "${NO_VERIFY:-0}" = 1 ] && exit 0
echo "== verify: unpack, install into a temporary prefix, audit"
( cd "$OUT" && sha256sum -c --ignore-missing SHA256SUMS )
mkdir -p "$TMP/unpack" "$TMP/home"
tar -xzf "$OUT/$NAME.tar.gz" -C "$TMP/unpack"
U="$TMP/unpack/MoonlightXRNative"
[ "$(cat "$U/VERSION")" = "$VER" ] || { echo "VERSION mismatch"; exit 1; }
# the zip holds the same tree, with install.sh and the binaries still executable after unpacking
mkdir -p "$TMP/unzip"
python3 -I - "$OUT/$NAME.zip" "$TMP/unzip" <<'PY'
import os, sys, zipfile
with zipfile.ZipFile(sys.argv[1]) as z:
    for i in z.infolist():
        z.extract(i, sys.argv[2])
        m = (i.external_attr >> 16) & 0o7777
        if m and not i.is_dir():
            os.chmod(os.path.join(sys.argv[2], i.filename), m)
PY
diff -r "$U" "$TMP/unzip/MoonlightXRNative" >/dev/null || { echo "the zip differs from the tarball"; diff -rq "$U" "$TMP/unzip/MoonlightXRNative" | head; exit 1; }
for f in install.sh app/moonlight-xr app/mlxr-cli app/moonlight-xr.sh; do
    [ -x "$TMP/unzip/MoonlightXRNative/$f" ] || { echo "$f is not executable in the zip"; exit 1; }
done
# twice: the second run must change nothing and still succeed
for i in 1 2; do
    DEST="$TMP/inst/app" NO_SHORTCUT=1 MLXR_CONFIG_DIR="$TMP/cfg" MLXR_INSTALL_LOG="$TMP/install.log" "$U/install.sh" > "$TMP/install.$i.log"
done
tail -n 4 "$TMP/install.2.log"
# (install.sh makes the Frame controller models from the local SteamVR, so the install may have them and the tarball never does)
[ -z "$(find "$U" -name 'frame_controller_*' -print -quit)" ] || { echo "the tarball holds a Steam Frame controller model"; exit 1; }
diff -r "$U/app" "$TMP/inst/app" -x LICENSE.txt -x CREDITS.md -x steam_cef.py -x 'frame_controller_*' >/dev/null || { echo "installed tree differs from the tarball"; diff -rq "$U/app" "$TMP/inst/app" -x 'frame_controller_*' | head; exit 1; }
for f in LICENSE.txt CREDITS.md; do cmp "$U/$f" "$TMP/inst/app/$f"; done
# install.sh puts steam_cef.py (top level in the tarball) beside the flat launcher
[ ! -x "$U/app/moonlight-xr-launcher" ] || cmp "$U/steam_cef.py" "$TMP/inst/app/steam_cef.py"
MLXR_CONFIG_DIR="$TMP/cfg" "$TMP/inst/app/mlxr-cli" 2>&1 | head -n 3 || true
"$TMP/inst/app/mlxr-gamepad-probe" --help >/dev/null
if command -v pacman >/dev/null; then
    ./tools/ldd-audit.sh "$TMP/inst/app/moonlight-xr"
    ./tools/ldd-audit.sh "$TMP/inst/app/mlxr-cli"
    ./tools/ldd-audit.sh "$TMP/inst/app/mlxr-gamepad-probe"
else
    echo "no pacman: ldd audit skipped"
fi
echo "release ok: $OUT/$NAME.tar.gz and $OUT/$NAME.zip"
