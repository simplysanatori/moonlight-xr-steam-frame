#!/bin/bash
# Builds the release tarball and zip and checks them. GPLv3.
#   tools/release.sh
# Writes moonlight-xr-steam-frame-<version>-linux-arm64.tar.gz, the same tree as
# moonlight-xr-steam-frame-<version>-linux-arm64.zip (same files, same modes; for the community
# installers Frametanium and FrameDrop, see docs/dev/reports/packaging.md) and SHA256SUMS into
# $OUT (default ~/Games/MoonlightXRNative/releases). Both archives unpack to
# MoonlightXRNative/ holding install.sh, steam_cef.py, dock_icon.py, README.md, docs/ (user guide,
# changelog, building, privacy audit), the licence files and
# app/ (binaries incl. mlxr-gamepad-probe, launcher, assets, the Moonlight Qt import script, and lib/
# with the bundled FFmpeg and libxml2) and the Frame controller model converter. The converted Steam Frame controller
# models (Valve's) are never in the tarball: install.sh makes them on the target.
# The version is the top-level VERSION file (e.g. 1.0.0, tag v1.0.0).
# When HEAD carries a v* tag it must match VERSION.
#
# Expects a finished build (./build.sh). Unless NO_VERIFY=1 it then unpacks the
# tarball (and the zip, which must give the same tree) into a temporary directory, runs its install.sh there (NO_SHORTCUT=1,
# a temporary DEST and config dir), compares every installed file with the
# tarball, and runs the ldd audit on the unpacked binaries (skipped on hosts
# without pacman).
# app/lib: the shared libraries SteamOS updates move to new sonames (FFmpeg, libxml2), built by
# tools/build-bundled-libs.sh; the binaries load them first through their RUNPATH $ORIGIN/lib. Every
# soname in $BUILD/lib (CMake links it to the bundle) goes in as a regular file under that name, not a
# symlink (some zip unpackers turn symlinks into small text files). A build without the bundle is
# refused unless NO_BUNDLE=1 (CI's build check on Ubuntu, which is not for the Frame).
# A release must carry the flat launcher ($LAUNCHER_BUILD, default build-launcher/, from launcher/build.sh);
# ALLOW_NO_LAUNCHER=1 makes a
# test tarball without it (CI), which is not for publishing.
# Environment: BUILD (default build), LAUNCHER_BUILD (default build-launcher), OUT, NO_VERIFY, NO_BUNDLE,
# ALLOW_NO_LAUNCHER.
set -euo pipefail
cd "$(dirname "$0")/.."
BUILD="${BUILD:-build}"
LAUNCHER_BUILD="${LAUNCHER_BUILD:-build-launcher}"
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
if [ -x "$LAUNCHER_BUILD/moonlight-xr-launcher" ]; then
    strip_to "$LAUNCHER_BUILD/moonlight-xr-launcher" "$ROOT/app/moonlight-xr-launcher"
    install -m755 launcher/moonlight-xr-launcher.sh "$ROOT/app/moonlight-xr-launcher.sh"
elif [ "${ALLOW_NO_LAUNCHER:-0}" = 1 ]; then
    echo "warning: ALLOW_NO_LAUNCHER=1: no flat launcher in this tarball (a test build, not a release)"
else
    echo "no $LAUNCHER_BUILD/moonlight-xr-launcher: a release must carry the flat launcher (launcher/build.sh)."
    echo "ALLOW_NO_LAUNCHER=1 makes a test tarball without it (CI's build checks)."
    exit 1
fi
# the bundled libraries (see the top): the sonames, as regular files
if [ -d "$BUILD/lib" ]; then
    mkdir -p "$ROOT/app/lib"
    for f in "$BUILD"/lib/lib*.so.*; do
        n="${f##*/}"
        [[ "$n" =~ ^lib[^.]+\.so\.[0-9]+$ ]] || continue
        strip_to "$f" "$ROOT/app/lib/$n"
    done
    [ -n "$(ls -A "$ROOT/app/lib")" ] || { echo "$BUILD/lib holds no shared library"; exit 1; }
    echo "bundled in app/lib: $(cd "$ROOT/app/lib" && ls | tr '\n' ' ')($(du -sh "$ROOT/app/lib" | cut -f1))"
elif [ "${NO_BUNDLE:-0}" = 1 ]; then
    echo "warning: NO_BUNDLE=1: no app/lib, the binaries load the system's FFmpeg and libxml2 (not for a release)"
else
    echo "no $BUILD/lib: the build links the system's FFmpeg and libxml2, which SteamOS updates replace."
    echo "Build with the bundled ones (./build.sh builds them; tools/build-bundled-libs.sh), or NO_BUNDLE=1 for a test tarball."
    exit 1
fi
# every bundled library a shipped binary needs is in app/lib (where a tool can read the ELF)
if command -v objdump >/dev/null && [ -d "$ROOT/app/lib" ]; then
    for b in "$ROOT"/app/moonlight-xr "$ROOT"/app/mlxr-cli "$ROOT"/app/mlxr-gamepad-probe "$ROOT"/app/moonlight-xr-launcher; do
        [ -f "$b" ] && objdump -p "$b" >/dev/null 2>&1 || continue
        for n in $(objdump -p "$b" | awk '$1 == "NEEDED" { print $2 }' | grep -E '^lib(av[a-z]+|sw[a-z]+|xml2)\.so\.' || true); do
            [ -f "$ROOT/app/lib/$n" ] || { echo "${b##*/} needs $n, which is not in app/lib (built against another FFmpeg or libxml2?)"; exit 1; }
        done
    done
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
# the installed binaries take every bundled library from their own lib/, never from the system
if [ -d "$TMP/inst/app/lib" ]; then
    L="$(readlink -f "$TMP/inst/app/lib")"
    for b in moonlight-xr mlxr-cli mlxr-gamepad-probe moonlight-xr-launcher; do
        [ -x "$TMP/inst/app/$b" ] || continue
        env -u LD_LIBRARY_PATH ldd "$TMP/inst/app/$b" > "$TMP/ldd.txt" 2>&1 || { cat "$TMP/ldd.txt"; echo "ldd $b failed"; exit 1; }
        ! grep -q 'not found' "$TMP/ldd.txt" || { grep 'not found' "$TMP/ldd.txt"; echo "$b: unresolved libraries"; exit 1; }
        for n in $(ls "$TMP/inst/app/lib"); do
            grep -q "^[[:space:]]*$n => " "$TMP/ldd.txt" || continue
            grep -qF "$n => $L/$n " "$TMP/ldd.txt" \
                || { grep "$n" "$TMP/ldd.txt"; echo "$b loads $n from outside its lib/"; exit 1; }
        done
    done
    # it starts: every library loads and main() runs (a usage error, before any XR)
    # a usage error exits non-zero by design: what counts is that it ran far enough to say so
    START="$(env -u LD_LIBRARY_PATH "$TMP/inst/app/moonlight-xr" --network bogus 2>&1 || true)"
    grep -q 'MLXR_ERROR: --network takes' <<<"$START" \
        || { echo "the installed moonlight-xr does not start"; head -n 5 <<<"$START"; exit 1; }
    echo "installed binaries load app/lib: $(ls "$TMP/inst/app/lib" | tr '\n' ' ')"
fi
if command -v pacman >/dev/null; then
    ./tools/ldd-audit.sh "$TMP/inst/app/moonlight-xr"
    ./tools/ldd-audit.sh "$TMP/inst/app/mlxr-cli"
    ./tools/ldd-audit.sh "$TMP/inst/app/mlxr-gamepad-probe"
    [ ! -x "$TMP/inst/app/moonlight-xr-launcher" ] || ./tools/ldd-audit.sh "$TMP/inst/app/moonlight-xr-launcher"
else
    echo "no pacman: ldd audit skipped"
fi
echo "release ok: $OUT/$NAME.tar.gz and $OUT/$NAME.zip"
