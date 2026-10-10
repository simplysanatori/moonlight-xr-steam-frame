#!/bin/bash
# Screenshots of the computer grid, an app grid and the settings page from stock moonlight-qt 6.1.0
# (tests/launcher/build-stock.sh) and from the launcher (hand-off disabled paths untouched), same
# synthetic config, offscreen; then a pixel comparison. Needs the launcher and the stock build.
#   compare-stock.sh <moonlight-xr-launcher> <mlxr-cli> <out-dir>
# The owner's flatpak is not used: running it would write its config. GPLv3.
set -uo pipefail
L="$1"; CLI="$2"; OUT="$3"
here="$(cd "$(dirname "$0")" && pwd)"
STOCK="$here/../../build-launcher/stock/obj/app/moonlight"
[ -x "$STOCK" ] && [ -x "$L" ] || { echo "SKIP: need the launcher and tests/launcher/build-stock.sh"; exit 77; }
mkdir -p "$OUT"; t="$(mktemp -d)"; trap 'rm -rf "$t"' EXIT
unset LD_LIBRARY_PATH XDG_CONFIG_HOME XDG_CACHE_HOME MLXR_CONFIG_DIR MLXR_NATIVE_CLIENT
export QT_QPA_PLATFORM=offscreen SDL_VIDEODRIVER=dummy QT_QUICK_BACKEND=software
MLXR_CONFIG_DIR="$t/q" "$CLI" identity >/dev/null 2>&1; MLXR_CONFIG_DIR="$t/s" "$CLI" identity >/dev/null 2>&1
python3 -I "$here/../backend/make_qt_conf.py" "$t/c.conf" "$t/q/client.pem" "$t/q/client.key" "$t/s/client.pem"
python3 -I - "$t/c.conf" <<'PY'
import sys
p=sys.argv[1]; o=[]
for l in open(p,'rb').read().split(b'\n'):
    o.append(l)
    if l==b'[General]': o.append(b'mdns=false')
    if l==b'[hosts]': o.append(b'1\\apps\\size=2')
d=b'\n'.join(o)
for a,b in ((b'203.0.113.5',b'198.51.100.5'),(b'203.0.113.7',b'198.51.100.7'),(b'192.0.2.10',b'198.51.100.77')): d=d.replace(a,b)
open(p,'wb').write(d)
PY
shoot() { # name binary extra-env...
    local name="$1" bin="$2"; shift 2
    local home="$t/home-$name"; mkdir -p "$home/.config/Moonlight Game Streaming Project"
    cp "$t/c.conf" "$home/.config/Moonlight Game Streaming Project/Moonlight.conf"
    for page in pcs settings apps; do
        local pg="$page"; [ $page = pcs ] && pg=""
        env HOME="$home" "$@" MLXR_LAUNCHER_PAGE="$pg" MLXR_LAUNCHER_SCREENSHOT="$OUT/$name-$page.png@3500" \
            timeout 60 "$bin" >"$t/$name-$page.log" 2>&1
    done
}
shoot stock "$STOCK"
shoot launcher "$L" MLXR_LAUNCHER_PROFILE=Moonlight MLXR_NO_SEED=1
rc=0
for page in pcs settings apps; do
    printf '%s: ' "$page"; python3 -I "$here/pngdiff.py" "$OUT/stock-$page.png" "$OUT/launcher-$page.png" || rc=1
done
exit $rc
