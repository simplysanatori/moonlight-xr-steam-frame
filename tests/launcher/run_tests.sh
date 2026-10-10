#!/bin/bash
# Headless tests of the flat launcher (patched moonlight-qt) and its hand-off to the VR client.
#   run_tests.sh <moonlight-xr-launcher> <mlxr-cli> [shots-dir]
# Everything runs in a temp HOME with a synthetic Moonlight.conf: no flatpak config is read, no
# network is used, no Steam/SteamVR is touched. Exit 77 = skipped (no launcher built). GPLv3.
set -uo pipefail
L="${1:-}"; CLI="${2:-}"; SHOTS="${3:-}"
here="$(cd "$(dirname "$0")" && pwd)"
[ -x "$L" ] || { echo "SKIP: no launcher binary ($L); run launcher/build.sh"; exit 77; }
[ -x "$CLI" ] || { echo "SKIP: no mlxr-cli"; exit 77; }
t="$(mktemp -d)"; trap 'rm -rf "$t"' EXIT
unset LD_LIBRARY_PATH XDG_CONFIG_HOME XDG_CACHE_HOME MLXR_CONFIG_DIR MLXR_NATIVE_CLIENT MLXR_HANDOFF MLXR_LAUNCHER_DRYRUN
export HOME="$t/home" QT_QPA_PLATFORM=offscreen SDL_VIDEODRIVER=dummy QT_QUICK_BACKEND=software
export MLXR_LOG_DIR="$t/log" MLXR_CONFIG_DIR="$t/native"
mkdir -p "$HOME" "$MLXR_LOG_DIR"
fails=0
fail() { echo "FAIL: $*" >&2; fails=$((fails+1)); }
ok() { echo "ok: $*"; }
expect() { # name, haystack, needle
    case "$2" in *"$3"*) ok "$1";; *) fail "$1: wanted [$3] in [$2]";; esac
}
refute() {
    case "$2" in *"$3"*) fail "$1: did not want [$3] in [$2]";; *) ok "$1";; esac
}

# --- synthetic Moonlight Qt config: a client identity, a paired PC with apps, an unpaired PC
MLXR_CONFIG_DIR="$t/qtclient" "$CLI" identity >/dev/null 2>&1
MLXR_CONFIG_DIR="$t/fakesrv" "$CLI" identity >/dev/null 2>&1
python3 -I "$here/../backend/make_qt_conf.py" "$t/Moonlight.conf" "$t/qtclient/client.pem" "$t/qtclient/client.key" "$t/fakesrv/client.pem"
# Qt writes the array sizes that the synthetic file (written for the importer) leaves out
python3 -I - "$t/Moonlight.conf" <<'PY'
import sys
p=sys.argv[1]; s=open(p,'rb').read().split(b'\n'); o=[]
for l in s:
    o.append(l)
    if l==b'[hosts]': o.append(b'1\\apps\\size=2')
open(p,'wb').write(b'\n'.join(o))
PY
# documentation addresses only: nothing here may reach a real PC
sed -i 's/100\.68\.121\.5/198.51.100.5/; s/203\.0\.113\.7/198.51.100.7/; s/192\.168\.0\.77/198.51.100.77/' "$t/Moonlight.conf"
cp "$t/Moonlight.conf" "$t/Moonlight.conf.orig"
export MLXR_SEED_CONF="$t/Moonlight.conf"
HOSTID=EXAMPLE0-example-id1-F307-92C54E613C61
CONF="$HOME/.config/Moonlight XR Native/Moonlight XR Native.conf"

setconf() { # key=value ... in [General] of the launcher's config
    python3 -I - "$CONF" "$@" <<'PY'
import sys,re
p=sys.argv[1]; kv=dict(a.split('=',1) for a in sys.argv[2:])
s=open(p).read().split('\n'); out=[]; sec=None; done=set()
for l in s:
    m=re.match(r'^\[(.*)\]$',l)
    if m:
        if sec=='General':
            out+= [k+'='+v for k,v in kv.items() if k not in done]
        sec=m.group(1)
    elif sec=='General' and '=' in l and l.split('=',1)[0] in kv:
        k=l.split('=',1)[0]; l=k+'='+kv[k]; done.add(k)
    out.append(l)
open(p,'w').write('\n'.join(out))
PY
}
selftest() { # spec -> stdout of the launcher
    MLXR_LAUNCHER_SELFTEST="$1" "$L" 2>"$t/stderr"
}
dry() { # spec -> the command line it would run
    MLXR_LAUNCHER_DRYRUN=1 MLXR_NATIVE_CLIENT= MLXR_LAUNCHER_DRYRUN_OUT="$t/dry.out" selftest "$1" >"$t/stdout"; rc=$?
    [ $rc = 0 ] || fail "dry run exit $rc: $(cat "$t/stdout" "$t/stderr")"
    grep '^MLXR_LAUNCHER_DRYRUN: ' "$t/stdout" | tail -1 | sed 's/^MLXR_LAUNCHER_DRYRUN: //'
}

# --- 1. first run: the launcher's own config is made from Moonlight Qt's, which stays untouched
[ ! -e "$CONF" ] || fail "config existed before the first run"
out="$(dry "DESKTOP-EXAMPLE:Desktop")"
echo "first run: $out"
[ -f "$CONF" ] && ok "launcher config created at $CONF" || fail "launcher config missing"
cmp -s "$t/Moonlight.conf" "$t/Moonlight.conf.orig" && ok "source config unmodified" || fail "source config modified"
grep -q 'DESKTOP-EXAMPLE' "$CONF" && grep -q 'EXAMPLE0' "$CONF" && ok "seeded PC list is in the launcher config" || fail "hosts not copied"
grep -q '^width=2560' "$CONF" && grep -q '^fps=90' "$CONF" && grep -q '^bitrate=80000' "$CONF" && ok "stream settings set for the headset" || fail "VR defaults not applied"
grep -q '^certificate=' "$CONF" && ok "identity copied" || fail "identity not copied"
expect "1440p90 80 Mbps, HEVC (no --h264)" "$out" "--host $HOSTID --app 881448767 --mode 2560x1440x90 --bitrate 80000 --exit-to-caller"
refute "no h264 flag by default" "$out" "--h264"
case "$out" in *moonlight-xr\ --host*) ok "program is moonlight-xr";; *) fail "program";; esac

# --- 2. the pairing the native client gets
cmp "$t/qtclient/client.pem" "$MLXR_CONFIG_DIR/client.pem" && ok "client.pem is the launcher's identity" || fail "client.pem"
cmp "$t/qtclient/client.key" "$MLXR_CONFIG_DIR/client.key" && ok "client.key is the launcher's identity" || fail "client.key"
cmp "$t/fakesrv/client.pem" "$MLXR_CONFIG_DIR/servercerts/$HOSTID.pem" && ok "pinned server cert shared" || fail "server cert"
[ "$(stat -c %a "$MLXR_CONFIG_DIR/client.key")" = 600 ] && [ "$(stat -c %a "$MLXR_CONFIG_DIR/hosts.ini")" = 600 ] && ok "private files are 0600" || fail "modes"
h="$MLXR_CONFIG_DIR/hosts.ini"
grep -q "^\[$HOSTID\]" "$h" && grep -q '^name = DESKTOP-EXAMPLE' "$h" && grep -q '^mac = 02:00:00:00:00:01' "$h" && grep -q '^cert = servercerts/' "$h" && ok "hosts.ini record" || { fail "hosts.ini"; cat "$h"; }
grep -q '^address = 198.51.100.5' "$h" && grep -q 'extra_addresses = .*198.51.100.7' "$h" && ok "addresses (no loopback)" || { fail "addresses"; cat "$h"; }
"$CLI" identity 2>&1 | grep -q "saved hosts:  1" && ok "mlxr-cli reads the exported host" || fail "mlxr-cli: $("$CLI" identity 2>&1)"
"$CLI" identity 2>&1 | grep -q "created a new client identity" && fail "identity regenerated"
# another host already in hosts.ini is kept
printf '\n[OTHER-UUID]\nname = other\naddress = 10.9.9.9\n' >> "$h"
dry "DESKTOP-EXAMPLE:Desktop" >/dev/null
grep -q '^\[OTHER-UUID\]' "$h" && [ "$(grep -c "^\[$HOSTID\]" "$h")" = 1 ] && ok "other hosts kept, no duplicates" || fail "hosts.ini merge"

# --- 3. the settings matrix (user changes them in Settings; here in the conf, as Settings would)
setconf width=1920 height=1080 fps=60 bitrate=20000 videocfg=0
out="$(dry "DESKTOP-EXAMPLE:Desktop")"; echo "1080p60 auto:  $out"
expect "1080p60" "$out" "--mode 1920x1080x60 --bitrate 20000"
refute "auto codec: no --h264" "$out" "--h264"
setconf width=2560 height=1440 fps=90 bitrate=80000 videocfg=1
out="$(dry "DESKTOP-EXAMPLE:Desktop")"; echo "1440p90 H.264: $out"
expect "1440p90" "$out" "--mode 2560x1440x90 --bitrate 80000"
expect "forced H.264" "$out" "--h264"
setconf videocfg=2
out="$(dry "DESKTOP-EXAMPLE:Desktop")"; refute "forced HEVC: no --h264" "$out" "--h264"
setconf videocfg=4
out="$(dry "DESKTOP-EXAMPLE:Desktop")"; refute "AV1 choice: not passed" "$out" "--h264"
setconf videocfg=0 videodec=2 audiocfg=1 packetsize=1024
out="$(dry "DESKTOP-EXAMPLE:Desktop")"; echo "sw/5.1/pkt:    $out"
expect "software decode" "$out" "--sw-decode"; expect "5.1" "$out" "--audio 51"; expect "packet size" "$out" "--packet-size 1024"
setconf videodec=0 audiocfg=2 packetsize=0
out="$(dry "DESKTOP-EXAMPLE:Desktop")"; expect "7.1" "$out" "--audio 71"; refute "no packet size" "$out" "--packet-size"
setconf audiocfg=0
out="$(dry "DESKTOP-EXAMPLE:Assassin's Creed Origins")"; expect "app by name -> id" "$out" "--app 1439608285"
out="$(MLXR_LAUNCHER_EXTRA_ARGS="--seconds 5" dry "DESKTOP-EXAMPLE:Desktop")"; expect "extra args" "$out" "--seconds 5"
grep -q '^width=1920' "$CONF" && fail "reseeded over the user's settings" || ok "later runs do not reseed"

# --- 4. a client that runs: control comes back with its result
fake="$t/fake-client.sh"
cat >"$fake" <<'FAKE'
#!/bin/bash
echo "args: $*" >> "$FAKE_LOG"
echo "cwd: $(pwd)" >> "$FAKE_LOG"
echo "ld_library_path: [${LD_LIBRARY_PATH-unset}]" >> "$FAKE_LOG"
sleep "${FAKE_SLEEP:-0.5}"
case "${FAKE_MODE:-ok}" in
  ok) exit 0;;
  err) echo "some log line"; echo "MLXR_ERROR: The host is on fire" >&2; exit 6;;
  silent) exit 3;;
  disc) exit 11;;
  crash) kill -SEGV $$;;
esac
FAKE
chmod +x "$fake"
export FAKE_LOG="$t/fake.log"
LD_LIBRARY_PATH=/steam/runtime MLXR_NATIVE_CLIENT="$fake" selftest "DESKTOP-EXAMPLE:Desktop" >"$t/stdout"; rc=$?
cat "$t/stdout"
[ $rc = 0 ] && ok "client exit 0 -> success" || fail "client exit 0 gave $rc"
expect "started signal and finish" "$(cat "$t/stdout")" "started=yes finished=yes error=none"
expect "client got the command line" "$(cat "$FAKE_LOG")" "--host $HOSTID --app 881448767 --mode 2560x1440x90 --bitrate 80000 --exit-to-caller"
expect "client ran in its own directory" "$(cat "$FAKE_LOG")" "cwd: $t"
expect "LD_LIBRARY_PATH not passed on" "$(cat "$FAKE_LOG")" "ld_library_path: [unset]"
grep -q "launcher runs" "$MLXR_LOG_DIR/moonlight-console.log" && ok "client output goes to moonlight-console.log" || fail "console log"
FAKE_MODE=err MLXR_NATIVE_CLIENT="$fake" selftest "DESKTOP-EXAMPLE:Desktop" >"$t/stdout"; rc=$?
[ $rc = 1 ] && ok "client failure -> error (exit 1)" || fail "exit code for failure was $rc"
expect "client error message reaches the dialog" "$(cat "$t/stdout")" "error=The host is on fire"
FAKE_MODE=silent MLXR_NATIVE_CLIENT="$fake" selftest "DESKTOP-EXAMPLE:Desktop" >"$t/stdout"; rc=$?
expect "code without a message" "$(cat "$t/stdout")" "stopped with code 3"
FAKE_MODE=disc MLXR_NATIVE_CLIENT="$fake" selftest "DESKTOP-EXAMPLE:Desktop" >"$t/stdout"; rc=$?
[ $rc = 0 ] && expect "disconnect chord (exit 11) is not an error" "$(cat "$t/stdout")" "error=none" || fail "exit 11 gave $rc"
FAKE_MODE=crash MLXR_NATIVE_CLIENT="$fake" selftest "DESKTOP-EXAMPLE:Desktop" >"$t/stdout"; rc=$?
expect "crash reported" "$(cat "$t/stdout")" "crashed"
MLXR_LAUNCHER_DRYRUN="fail:5:no such app" selftest "DESKTOP-EXAMPLE:Desktop" >"$t/stdout"; expect "dry-run failure injection" "$(cat "$t/stdout")" "error=no such app"
selftest "nosuch:Desktop" >/dev/null 2>&1; [ $? = 20 ] && ok "unknown PC is reported" || fail "unknown PC"
# a PC without that app
selftest "laptop:1" >/dev/null 2>&1; [ $? = 21 ] && ok "unknown app is reported" || fail "unknown app"

# --- 5. the whole app starts headless, shows the seeded PCs, and leaves cleanly
if [ -n "$SHOTS" ]; then
    mkdir -p "$SHOTS"
    setconf mdns=false   # no discovery: the UI tests must not talk to real PCs
    # with the hand-off active (a client is found): no local decoder probe, so no hardware-decoder warning
    MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_SCREENSHOT="$SHOTS/pcs.png@2500" "$L" >"$t/ui.out" 2>&1; rc=$?
    [ $rc = 0 ] && ok "UI starts, loads, exits (offscreen)" || fail "UI exit $rc"
    QMLERR='TypeError|ReferenceError|is not a function|is not defined|Cannot assign|Unable to assign|qrc:/[^ ]*(Error|error)'
    grep -Eq "$QMLERR" "$t/ui.out" && { fail "QML errors:"; grep -E "$QMLERR" "$t/ui.out" | head; } || ok "no QML errors on start"
    grep -q "Screenshot.*saved" "$t/ui.out" && ok "screenshot written" || fail "no screenshot"
    grep -q "Failed to find ANY working\|Unable to find working decoder" "$t/ui.out" && fail "decoder probe ran in hand-off mode" || ok "hand-off mode: no local decoder probe"
    MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_PAGE=settings MLXR_LAUNCHER_SCREENSHOT="$SHOTS/settings.png@3000" "$L" >"$t/ui3.out" 2>&1
    grep -Eq "$QMLERR" "$t/ui3.out" && { fail "settings QML errors"; grep -E "$QMLERR" "$t/ui3.out" | head; } || ok "settings page loads in hand-off mode (no QML errors)"
    # MLXR_HANDOFF=0: the stock probe runs again
    MLXR_HANDOFF=0 MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_SCREENSHOT="$SHOTS/pcs-stock-mode.png@2500" "$L" >"$t/ui4.out" 2>&1
    grep -q "Unable to find working decoder\|Failed to initialize VAAPI" "$t/ui4.out" && ok "hand-off off: stock decoder probe runs" || fail "stock probe missing with MLXR_HANDOFF=0"
    # the segue with a running fake client, then the client failing: dialog
    FAKE_SLEEP=4 FAKE_MODE=err MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_DEMO="DESKTOP-EXAMPLE:Desktop" \
      MLXR_LAUNCHER_SCREENSHOT="$SHOTS/segue.png@2500,$SHOTS/after.png@7500" "$L" >"$t/ui2.out" 2>&1; rc=$?
    [ $rc = 0 ] && ok "segue demo ran (client started, failed, UI came back)" || fail "segue demo exit $rc"
    grep -E "$QMLERR" "$t/ui2.out" && fail "segue QML errors" || ok "no QML errors in the segue"
    grep -q "The VR client exited with 6" "$t/ui2.out" && ok "client failure seen by the UI" || fail "no exit seen: $(tail -5 "$t/ui2.out")"
fi
echo "failures: $fails"
[ $fails = 0 ]
