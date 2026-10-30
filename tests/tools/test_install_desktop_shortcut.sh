#!/bin/bash
# packaging/install.sh's "Desktop in VR" shortcut (PLAN 6.4) against a fake Steam (tests/tools/fake_cef.py): with
# --desktop-shortcut a second shortcut runs the flat launcher with `stream "PC" "Desktop"` for the PC of the
# launcher's last stream (or --desktop-pc), and the main shortcut is still the one found by its Exe; without the
# option one line offers it and nothing is added. Binaries are copies of /bin/true. GPLv3.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
t="$(mktemp -d)"; CEFPID=""
trap '[ -n "$CEFPID" ] && kill "$CEFPID" 2>/dev/null; rm -rf "$t"' EXIT
fails=0
fail() { echo "FAIL: $*"; fails=$((fails+1)); }
ok() { echo "ok: $*"; }
expect() { case "$2" in *"$3"*) ok "$1";; *) fail "$1: wanted [$3] in [$2]";; esac; }
refute() { case "$2" in *"$3"*) fail "$1: did not want [$3] in [$2]";; *) ok "$1";; esac; }

# an unpacked release, as install.sh finds it
TRUE=/usr/bin/true; [ -x "$TRUE" ] || TRUE=/bin/true
R="$t/rel"; mkdir -p "$R/app/assets/branding"
cp "$ROOT/packaging/install.sh" "$ROOT/packaging/steam_cef.py" "$R/"
for f in moonlight-xr mlxr-cli moonlight-xr-launcher; do cp "$TRUE" "$R/app/$f"; done
cp "$ROOT/packaging/moonlight-xr.sh" "$ROOT/launcher/moonlight-xr-launcher.sh" "$ROOT/tools/bug-report.sh" "$R/app/"
cp "$ROOT/backend/seed-from-moonlight-qt.sh" "$ROOT/backend/seed_moonlight_qt.py" "$R/app/"
echo "1.0.0" > "$R/app/VERSION"

# the flat launcher's config: two PCs, the last stream went to gaming-pc
H="$t/home"; mkdir -p "$H/.config/Moonlight XR Native" "$H/.local/share/Steam/userdata/1/config"
printf '[hosts]\n1\\hostname=gaming-pc\n2\\hostname=laptop\nsize=2\n\n[mlxr]\nlast_pc=gaming-pc\n' > "$H/.config/Moonlight XR Native/Moonlight XR Native.conf"

# the fake Steam: the "Desktop in VR" shortcut's script answers 4242, the main one's 1111
printf '[["Desktop in VR", 4242], ["AddShortcut", 1111]]' > "$t/rules.json"
python3 -I "$ROOT/tests/tools/fake_cef.py" 0 "$t/rules.json" "$t/cef.log" > "$t/cef.out" 2>&1 &
CEFPID=$!
for _ in $(seq 50); do grep -q 'fake CEF on' "$t/cef.out" 2>/dev/null && break; sleep 0.1; done
PORT="$(sed -n 's/^fake CEF on //p' "$t/cef.out")"
[ -n "$PORT" ] || { cat "$t/cef.out"; echo "FAIL: fake CEF did not start"; exit 1; }

run() { # [install.sh options] -> output
    : > "$t/cef.log"
    ( cd "$R" && env -i PATH="$PATH" HOME="$H" STEAM_CEF_PORT="$PORT" MLXR_CONFIG_DIR="$t/cfg" DEST="$t/dest" \
        MLXR_INSTALL_LOG="$t/install.log" ./install.sh --art "$R/app/assets/branding" "$@" < /dev/null 2>&1 )
}

o="$(run)"; rc=$?
[ $rc = 0 ] && ok "install without the option exits 0" || fail "install exit $rc: $o"
expect "the main shortcut is made" "$o" 'Steam shortcut "Moonlight XR for Frame" appid 1111'
expect "one line offers the Desktop shortcut" "$o" 'Steam shortcut "Desktop in VR": not added (run again with --desktop-shortcut'
refute "and nothing is added" "$(cat "$t/cef.log")" "Desktop in VR"
expect "the summary is still the last line" "$(echo "$o" | tail -n 1)" "Steam shortcut ready (appid 1111)"

o="$(run --desktop-shortcut)"; rc=$?
[ $rc = 0 ] && ok "install with --desktop-shortcut exits 0" || fail "install exit $rc: $o"
expect "the Desktop shortcut is made for the last PC" "$o" 'Steam shortcut "Desktop in VR" appid 4242: Desktop on gaming-pc'
js="$(grep -A8 'const name="Desktop in VR"' "$t/cef.log")"
expect "it runs the flat launcher" "$js" "exe=\"\\\"$t/dest/moonlight-xr-launcher.sh\\\"\""
expect "with the command line's stream \"PC\" \"Desktop\"" "$js" 'opts="stream \"gaming-pc\" \"Desktop\""'
expect "it is found again by its name, not added twice" "$js" "find(a=>a.display_name===name)"
expect "its game resolution is set too" "$(cat "$t/cef.log")" "SetAppResolutionOverride(4242,'1920x1080')"
expect "the main shortcut keeps its launch options empty" "$(grep -A9 'const name="Moonlight XR for Frame"' "$t/cef.log")" "SetShortcutLaunchOptions(id, '')"
expect "the summary is still the last line" "$(echo "$o" | tail -n 1)" "Steam shortcut ready (appid 1111)"

o="$(run --desktop-pc "Office PC")"
expect "--desktop-pc names the PC" "$o" 'appid 4242: Desktop on Office PC'
expect "a PC name with a space is quoted" "$(grep 'Desktop in VR' "$t/cef.log")" 'opts="stream \"Office PC\" \"Desktop\""'

# no last stream and two PCs: no guess
printf '[hosts]\n1\\hostname=gaming-pc\n2\\hostname=laptop\nsize=2\n' > "$H/.config/Moonlight XR Native/Moonlight XR Native.conf"
o="$(run --desktop-shortcut)"
expect "no last PC and two PCs: skipped with how to name one" "$o" 'skipped, no PC yet (stream once from the flat launcher, or give --desktop-pc NAME)'
refute "and nothing is added" "$(cat "$t/cef.log")" "Desktop in VR"
# the only PC is taken
printf '[hosts]\n1\\hostname=gaming-pc\nsize=1\n' > "$H/.config/Moonlight XR Native/Moonlight XR Native.conf"
expect "the launcher's only PC" "$(run --desktop-shortcut)" 'appid 4242: Desktop on gaming-pc'
# without the flat launcher there is nothing to run
rm "$R/app/moonlight-xr-launcher" "$t/dest/moonlight-xr-launcher"
expect "no flat launcher: skipped" "$(run --desktop-shortcut)" 'skipped, it needs the flat launcher'

echo "failures: $fails"
[ $fails = 0 ]
