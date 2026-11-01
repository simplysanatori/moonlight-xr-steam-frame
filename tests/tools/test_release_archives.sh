#!/bin/bash
# tools/release.sh writes the .tar.gz and the .zip from one staged tree: both hold the same entries with the
# same modes and contents, both pass tools/privacy-check.sh, and the unpacked zip installs with no terminal and
# no Steam (the way Frametanium runs install.sh): exit 0, the files in place, the skipped shortcut said so in the
# output and in the log. The binaries are copies of /bin/true, so this needs no build and no Frame. GPLv3.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
t="$(mktemp -d)"; trap 'rm -rf "$t"' EXIT
fail() { echo "FAIL: $*"; exit 1; }

TRUE=/usr/bin/true; [ -x "$TRUE" ] || TRUE=/bin/true
mkdir -p "$t/build/backend" "$t/build/input" "$t/out" "$t/home"
for f in moonlight-xr backend/mlxr-cli input/mlxr-gamepad-probe; do cp "$TRUE" "$t/build/$f"; done
# the bundled libraries (as tools/build-bundled-libs.sh leaves them, a soname symlink to the real file; copies of
# the C library stand in): release.sh must ship each as app/lib/<soname>, a regular file. It insists on the
# FFmpeg and SDL2_ttf sonames being among them.
LIBC="$(ldd "$TRUE" | awk '$1 ~ /^libc\.so/ { print $3 }')"
[ -f "$LIBC" ] || fail "no C library found to stand in for a bundled one"
mkdir -p "$t/bundle"
for n in libmlxrtest.so.1 libavcodec.so.61 libSDL2_ttf-2.0.so.0; do
    cp "$LIBC" "$t/bundle/$n.3"; ln -s "$n.3" "$t/bundle/$n"
done
ln -s "$t/bundle" "$t/build/lib"
# the flat launcher comes staged with its Qt (tools/stage-launcher.sh) from LAUNCHER_STAGE, never from the tree's
# own build-launcher/. A release without it is refused; there is no way around that. NO_BUNDLE_CHECK=1 skips
# tools/check-bundle.sh, because these stand-ins are not real libraries (CI's release step runs the check).
export NO_BUNDLE_CHECK=1
mkdir -p "$t/nolauncher" "$t/stage/lib" "$t/stage/plugins/platforms" "$t/stage/qml/QtQuick" "$t/refused"
if BUILD="$t/build" LAUNCHER_STAGE="$t/nolauncher" OUT="$t/refused" NO_VERIFY=1 "$ROOT/tools/release.sh" > "$t/refused.out" 2>&1; then
    fail "release.sh made a tarball without the flat launcher"
fi
grep -q "a release carries the flat launcher" "$t/refused.out" || { cat "$t/refused.out"; fail "no reason given for refusing"; }
cp "$TRUE" "$t/stage/moonlight-xr-launcher"; cp "$LIBC" "$t/stage/lib/libQt6Core.so.6"
cp "$LIBC" "$t/stage/plugins/platforms/libqoffscreen.so"; echo x > "$t/stage/qml/QtQuick/qmldir"
printf '[Paths]\nPrefix = .\n' > "$t/stage/qt.conf"
BUILD="$t/build" LAUNCHER_STAGE="$t/stage" OUT="$t/out" NO_VERIFY=1 "$ROOT/tools/release.sh" > "$t/release.out" 2>&1 \
    || { cat "$t/release.out"; fail "release.sh failed"; }
tgz=("$t"/out/moonlight-xr-steam-frame-*-linux-arm64.tar.gz); zp=("$t"/out/moonlight-xr-steam-frame-*-linux-arm64.zip)
[ -f "${tgz[0]}" ] || fail "no .tar.gz in the output"
[ -f "${zp[0]}" ] || fail "no .zip in the output"
[ "$(basename "${tgz[0]}" .tar.gz)" = "$(basename "${zp[0]}" .zip)" ] || fail "the two archives have different base names"
for a in "${tgz[0]}" "${zp[0]}"; do
    grep -q " $(basename "$a")\$" "$t/out/SHA256SUMS" || fail "SHA256SUMS does not list $(basename "$a")"
done
( cd "$t/out" && sha256sum -c SHA256SUMS >/dev/null ) || fail "SHA256SUMS does not match"

# same file list, same kinds, same modes, same contents; one top-level folder; install.sh and the binaries executable
python3 -I - "${tgz[0]}" "${zp[0]}" <<'PY' || fail "archives differ"
import hashlib, stat, sys, tarfile, zipfile
tar_path, zip_path = sys.argv[1:3]
def from_tar():
    out = {}
    with tarfile.open(tar_path) as t:
        for m in t.getmembers():
            if m.isdir():
                out[m.name.rstrip("/")] = ("dir", m.mode & 0o7777, "")
            elif m.issym():
                out[m.name] = ("link", 0o777, m.linkname)
            else:
                out[m.name] = ("file", m.mode & 0o7777, hashlib.sha256(t.extractfile(m).read()).hexdigest())
    return out
def from_zip():
    out = {}
    with zipfile.ZipFile(zip_path) as z:
        assert z.testzip() is None, "zip fails its own CRC check"
        for i in z.infolist():
            mode = (i.external_attr >> 16)
            assert mode, "%s has no Unix mode in the zip" % i.filename
            if i.is_dir():
                out[i.filename.rstrip("/")] = ("dir", mode & 0o7777, "")
            elif stat.S_ISLNK(mode):
                out[i.filename] = ("link", 0o777, z.read(i).decode())
            else:
                out[i.filename] = ("file", mode & 0o7777, hashlib.sha256(z.read(i)).hexdigest())
    return out
a, b = from_tar(), from_zip()
bad = 0
for n in sorted(set(a) | set(b)):
    if a.get(n) != b.get(n):
        print("differs:", n, a.get(n), b.get(n)); bad += 1
assert a, "empty archive"
assert not bad, "%d entries differ" % bad
tops = {n.split("/")[0] for n in a}
assert tops == {"MoonlightXRNative"}, "top level is %s" % sorted(tops)
for n in ("MoonlightXRNative/install.sh", "MoonlightXRNative/app/moonlight-xr", "MoonlightXRNative/app/mlxr-cli",
          "MoonlightXRNative/app/moonlight-xr.sh", "MoonlightXRNative/app/bug-report.sh",
          "MoonlightXRNative/app/moonlight-xr-launcher", "MoonlightXRNative/app/moonlight-xr-launcher.sh"):
    assert a[n][1] & 0o111 == 0o111, "%s is not executable in the tar.gz (%o)" % (n, a[n][1])
    assert b[n][1] & 0o111 == 0o111, "%s is not executable in the zip (%o)" % (n, b[n][1])
lib = sorted(n for n in a if n.startswith("MoonlightXRNative/app/lib/"))
assert lib == ["MoonlightXRNative/app/lib/" + n for n in ("libQt6Core.so.6", "libSDL2_ttf-2.0.so.0", "libavcodec.so.61", "libmlxrtest.so.1")], "app/lib holds %s" % lib
assert all(a[n][0] == "file" and b[n][0] == "file" for n in lib), "an app/lib entry is not a regular file"
for n in ("qt.conf", "plugins/platforms/libqoffscreen.so", "qml/QtQuick/qmldir"):
    assert "MoonlightXRNative/app/" + n in a, "app/%s is missing" % n
print("same %d entries and modes in both archives" % len(a))
PY

"$ROOT/tools/privacy-check.sh" "${tgz[0]}" > "$t/priv.tar" 2>&1 || { cat "$t/priv.tar"; fail "privacy check on the .tar.gz"; }
mkdir "$t/unzip"
python3 -I - "${zp[0]}" "$t/unzip" <<'PY' || fail "unpacking the zip"
import os, sys, zipfile
with zipfile.ZipFile(sys.argv[1]) as z:
    for i in z.infolist():
        z.extract(i, sys.argv[2])
        m = (i.external_attr >> 16) & 0o7777
        if m and not i.is_dir():
            os.chmod(os.path.join(sys.argv[2], i.filename), m)
PY
"$ROOT/tools/privacy-check.sh" "$t/unzip" > "$t/priv.zip" 2>&1 || { cat "$t/priv.zip"; fail "privacy check on the .zip"; }

# installed from the unpacked zip: no terminal (setsid), stdin closed, no Steam (CEF port 1 refuses), a fresh HOME
mkdir "$t/cfg"
U="$t/unzip/MoonlightXRNative"
( cd "$U" && env -i PATH="$PATH" HOME="$t/home" STEAM_CEF_PORT=1 MLXR_CONFIG_DIR="$t/cfg" \
    setsid -w ./install.sh < /dev/null > "$t/install.out" 2>&1 )
rc=$?
[ "$rc" = 0 ] || { cat "$t/install.out"; fail "install.sh exited $rc without Steam"; }
D="$t/home/Games/MoonlightXRNative"
[ -x "$D/app/moonlight-xr" ] && [ -x "$D/app/mlxr-cli" ] && [ -d "$D/app/assets" ] && [ -x "$D/app/moonlight-xr-launcher" ] || { cat "$t/install.out"; fail "files not installed"; }
[ -f "$D/app/qt.conf" ] && [ -f "$D/app/plugins/platforms/libqoffscreen.so" ] && [ -f "$D/app/qml/QtQuick/qmldir" ] || fail "the launcher's Qt files (qt.conf, plugins, qml) are not installed"
[ -f "$D/install.log" ] || fail "no install.log under ~/Games/MoonlightXRNative"
cmp -s "$t/install.out" "$D/install.log" || fail "the log differs from the output"
grep -q "Steam shortcut: SKIPPED" "$t/install.out" || fail "the output does not say the shortcut was skipped"
tail -n 2 "$t/install.out" | grep -q "^Steam shortcut skipped" || { tail -n 3 "$t/install.out"; fail "the last lines do not give the summary"; }
[ -f "$D/app/lib/libmlxrtest.so.1" ] && [ ! -L "$D/app/lib/libmlxrtest.so.1" ] || fail "lib/libmlxrtest.so.1 not installed as a file"
# a library an older release had is gone after the next install (lib/ is mirrored)
cp "$D/app/lib/libmlxrtest.so.1" "$D/app/lib/libstale.so.7"
# a second run appends to the log and still succeeds
( cd "$U" && env -i PATH="$PATH" HOME="$t/home" STEAM_CEF_PORT=1 MLXR_CONFIG_DIR="$t/cfg" ./install.sh < /dev/null > "$t/install2.out" 2>&1 ) \
    || { cat "$t/install2.out"; fail "second run failed"; }
[ "$(grep -c '^== .* install.sh' "$D/install.log")" = 2 ] || fail "the log should hold two runs"
[ "$(LC_ALL=C ls "$D/app/lib" | tr '\n' ' ')" = "libQt6Core.so.6 libSDL2_ttf-2.0.so.0 libavcodec.so.61 libmlxrtest.so.1 " ] || fail "lib/ after the second install holds: $(ls "$D/app/lib" | tr '\n' ' ')"
[ ! -e "$D/app/lib.new" ] && [ ! -e "$D/app/lib.old" ] || fail "install left lib.new or lib.old behind"
cmp -s "$U/app/lib/libmlxrtest.so.1" "$D/app/lib/libmlxrtest.so.1" || fail "the installed library differs from the release's"
echo "release archives ok"
