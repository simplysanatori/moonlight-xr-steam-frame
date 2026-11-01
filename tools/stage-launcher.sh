#!/bin/bash
# The flat launcher with its own Qt, laid out as it goes into app/. GPLv3.
#   tools/stage-launcher.sh <Qt prefix> [out dir]       (out default: build-launcher/stage)
# Reads build-launcher/moonlight-xr-launcher (launcher/build.sh, built against that Qt) and the QML in
# build-launcher/src/app/gui. Writes:
#   moonlight-xr-launcher   RUNPATH $ORIGIN/lib only (qmake also wrote the build machine's Qt lib dir)
#   qt.conf                 Qt's prefix is the app dir: plugins/ and qml/ beside it, never the system's
#   lib/                    the Qt libraries and ICU the launcher, the plugins and the QML modules need,
#                           and any other library they need that is not on tools/host-libs.allow (copied
#                           from this machine, RUNPATH $ORIGIN); the app's own bundle (FFmpeg, SDL2_ttf;
#                           tools/build-bundled-libs.sh) is left to tools/release.sh
#   plugins/                the Qt plugins: platforms (xcb for Game Mode's Xwayland, wayland for the
#                           desktop, offscreen), their GL and shell integrations, TLS, image formats,
#                           icon engines, input contexts, network information
#   qml/                    the QML modules the launcher's pages import (qmlimportscanner), with the
#                           modules those import in turn
# Why: SteamOS updates replace the system's Qt (6.8 to 6.10) and drop libraries (SDL2_ttf), and a
# launcher built against them stops starting. Built in CI on Ubuntu 24.04 (glibc 2.39, the older
# SteamOS's), so it runs on both. Needs patchelf, objdump, python3.
set -euo pipefail
cd "$(dirname "$0")/.."
QT="$(cd "${1:?usage: stage-launcher.sh <Qt prefix> [out dir]}" && pwd)"
OUT="${2:-build-launcher/stage}"
LB=build-launcher
BUNDLE="${MLXR_BUNDLED_LIBS_DIR:-${MLXR_BUNDLE_ROOT:-$HOME/Games/MoonlightXRNative/third-party-build/bundled}/dist}"
for t in patchelf objdump strip python3 ldconfig; do command -v "$t" >/dev/null || { echo "missing host tool: $t"; exit 1; }; done
[ -x "$LB/moonlight-xr-launcher" ] || { echo "no $LB/moonlight-xr-launcher (launcher/build.sh)"; exit 1; }
[ -d "$LB/src/app/gui" ] || { echo "no $LB/src/app/gui"; exit 1; }
[ -f "$QT/lib/libQt6Core.so.6" ] || { echo "no Qt at $QT"; exit 1; }
mapfile -t PATS < <(sed -e 's/#.*//' -e 's/[[:space:]]//g' tools/host-libs.allow | grep .)
allowed() { local p; for p in "${PATS[@]}"; do [[ "$1" == $p ]] && return 0; done; return 1; }

rm -rf "$OUT"; mkdir -p "$OUT/lib" "$OUT/plugins" "$OUT/qml"
# stripped first: strip must never run on a file patchelf changed
strip --strip-unneeded -o "$OUT/moonlight-xr-launcher" "$LB/moonlight-xr-launcher"; chmod 755 "$OUT/moonlight-xr-launcher"
patchelf --set-rpath '$ORIGIN/lib' "$OUT/moonlight-xr-launcher"
cat > "$OUT/qt.conf" <<'EOF'
; Moonlight XR for Frame: the launcher's own Qt (tools/stage-launcher.sh), never the system's
[Paths]
Prefix = .
Libraries = lib
Plugins = plugins
QmlImports = qml
Qml2Imports = qml
EOF

echo "== plugins"
for d in platforms xcbglintegrations wayland-shell-integration wayland-graphics-integration-client \
         wayland-decoration-client tls imageformats iconengines platforminputcontexts networkinformation; do
    [ -d "$QT/plugins/$d" ] && cp -a "$QT/plugins/$d" "$OUT/plugins/"
done
# platforms: xcb (Game Mode runs X11 apps on Xwayland), wayland (the desktop), offscreen and minimal
find "$OUT/plugins/platforms" -name '*.so' ! -name 'libqxcb.so' ! -name 'libqwayland*.so' \
    ! -name 'libqoffscreen.so' ! -name 'libqminimal.so' -delete
for d in "$OUT"/plugins/*/; do echo "  ${d#"$OUT"/}: $(cd "$d" && ls | tr '\n' ' ')"; done

echo "== QML modules"
SCANNER="$QT/libexec/qmlimportscanner"; [ -x "$SCANNER" ] || SCANNER="$QT/bin/qmlimportscanner"
"$SCANNER" -rootPath "$LB/src/app/gui" -importPath "$QT/qml" > "$OUT/.imports.json"
python3 -I - "$QT/qml" "$OUT/qml" "$OUT/.imports.json" <<'PY'
import json, os, shutil, sys
src, dst, scan = sys.argv[1:4]
want = set()
for e in json.load(open(scan)):
    if e.get("type") == "module" and e.get("path", "").startswith(src):
        want.add(e["name"])
# Controls' default style and the one moonlight-qt picks (qtquickcontrols2.conf: Material)
want |= {"QtQuick.Controls.Basic", "QtQuick.Controls.Material"}
done = set()
while want - done:
    uri = sorted(want - done)[0]; done.add(uri)
    d = os.path.join(src, *uri.split("."))
    if not os.path.isfile(os.path.join(d, "qmldir")):
        sys.exit(f"QML module {uri} is not in {src}")
    # the module's own files; its child modules are copied only when imported
    out = os.path.join(dst, *uri.split("."))
    os.makedirs(out, exist_ok=True)
    for n in os.listdir(d):
        p = os.path.join(d, n)
        if os.path.isfile(p):
            shutil.copy2(p, os.path.join(out, n))
    # what it imports or depends on (an optional import only when it is the default, Controls' Basic)
    for line in open(os.path.join(d, "qmldir")):
        w = line.split()
        if not w: continue
        if w[0] == "optional":
            if len(w) > 2 and w[1] == "default" and w[2] == "import": w = w[2:]
            else: continue
        if w[0] == "default": w = w[1:]
        if len(w) > 1 and w[0] in ("import", "depends"):
            want.add(w[1])
print("  " + " ".join(sorted(done)))
PY
rm -f "$OUT/.imports.json"

echo "== libraries"
LDC="$(ldconfig -p)"   # (a pipe into awk with exit would kill ldconfig with SIGPIPE, and pipefail would end this script)
# every library the launcher, the plugins and the QML plugins need, until nothing new comes in
while :; do
    added=0
    while IFS= read -r -d '' f; do
        [ "$(head -c 4 "$f" | od -An -tx1 | tr -d ' \n')" = 7f454c46 ] || continue
        for s in $(objdump -p "$f" | awk '$1 == "NEEDED" { print $2 }'); do
            [ -e "$OUT/lib/$s" ] || [ -e "$BUNDLE/lib/$s" ] || allowed "$s" && continue
            if [ -e "$QT/lib/$s" ]; then
                cp -L "$QT/lib/$s" "$OUT/lib/$s"
            else
                p="$(awk -v s="$s" '$1 == s && /AArch64|aarch64/ { print $NF; exit }' <<<"$LDC")"
                [ -n "$p" ] || p="$(awk -v s="$s" '$1 == s { print $NF; exit }' <<<"$LDC")"
                [ -n "$p" ] || { echo "${f#"$OUT"/} needs $s: not in Qt, not on this machine, not allowlisted"; exit 1; }
                cp -L "$p" "$OUT/lib/$s"; chmod 755 "$OUT/lib/$s"
                patchelf --set-rpath '$ORIGIN' "$OUT/lib/$s"
                echo "  from this machine: $s ($p, needed by ${f#"$OUT"/})"
            fi
            added=1
        done
    done < <(find "$OUT" -type f -print0)
    [ "$added" = 1 ] || break
done
echo "  lib/: $(cd "$OUT/lib" && ls | tr '\n' ' ')"
echo "staged $OUT ($(du -sh "$OUT" | cut -f1))"
