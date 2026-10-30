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
BUILD="$t/build" OUT="$t/out" NO_VERIFY=1 "$ROOT/tools/release.sh" > "$t/release.out" 2>&1 \
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
          "MoonlightXRNative/app/moonlight-xr.sh", "MoonlightXRNative/app/bug-report.sh"):
    assert a[n][1] & 0o111 == 0o111, "%s is not executable in the tar.gz (%o)" % (n, a[n][1])
    assert b[n][1] & 0o111 == 0o111, "%s is not executable in the zip (%o)" % (n, b[n][1])
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
[ -x "$D/app/moonlight-xr" ] && [ -x "$D/app/mlxr-cli" ] && [ -d "$D/app/assets" ] || { cat "$t/install.out"; fail "files not installed"; }
[ -f "$D/install.log" ] || fail "no install.log under ~/Games/MoonlightXRNative"
cmp -s "$t/install.out" "$D/install.log" || fail "the log differs from the output"
grep -q "Steam shortcut: SKIPPED" "$t/install.out" || fail "the output does not say the shortcut was skipped"
tail -n 2 "$t/install.out" | grep -q "^Steam shortcut skipped" || { tail -n 3 "$t/install.out"; fail "the last lines do not give the summary"; }
# a second run appends to the log and still succeeds
( cd "$U" && env -i PATH="$PATH" HOME="$t/home" STEAM_CEF_PORT=1 MLXR_CONFIG_DIR="$t/cfg" ./install.sh < /dev/null > "$t/install2.out" 2>&1 ) \
    || { cat "$t/install2.out"; fail "second run failed"; }
[ "$(grep -c '^== .* install.sh' "$D/install.log")" = 2 ] || fail "the log should hold two runs"
echo "release archives ok"
