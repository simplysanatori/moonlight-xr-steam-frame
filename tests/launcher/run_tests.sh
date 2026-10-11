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

setconf() { # key=value ... in [General] (or [$SECTION]) of the launcher's config
    python3 -I - "$CONF" "${SECTION:-General}" "$@" <<'PY'
import sys,re
p=sys.argv[1]; want=sys.argv[2]; kv=dict(a.split('=',1) for a in sys.argv[3:])
s=open(p).read().split('\n'); out=[]; sec=None; done=set(); seen=False
for l in s:
    m=re.match(r'^\[(.*)\]$',l)
    if m:
        if sec==want:
            out+= [k+'='+v for k,v in kv.items() if k not in done]; done|=set(kv)
        sec=m.group(1); seen|= sec==want
    elif sec==want and '=' in l and l.split('=',1)[0] in kv:
        k=l.split('=',1)[0]; l=k+'='+kv[k]; done.add(k)
    out.append(l)
if sec==want: out+= [k+'='+v for k,v in kv.items() if k not in done]
elif not seen: out+= ['', '['+want+']'] + [k+'='+v for k,v in kv.items()]
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
expect "1440p90 80 Mbps, HEVC (no --h264)" "$out" "--host $HOSTID --app 881448767 --mode 2560x1440x90 --bitrate 80000 --fps match --network auto --exit-to-caller"
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
# an address the PC was known by (another network path) survives the next hand-off, so both paths stay candidates
sed -i "/^\[$HOSTID\]/,/^cert/ s/^extra_addresses = .*/&,198.51.100.27/" "$h"
grep -q 'extra_addresses = .*198.51.100.27' "$h" || fail "test setup: extra address not added"
dry "DESKTOP-EXAMPLE:Desktop" >/dev/null
grep -q '^address = 198.51.100.5' "$h" && grep -q 'extra_addresses = .*198.51.100.7' "$h" && grep -q 'extra_addresses = .*198.51.100.27' "$h" && ok "earlier addresses kept across hand-offs" || { fail "address merge"; cat "$h"; }
[ "$(grep -o '198.51.100.7' "$h" | wc -l)" = 1 ] && ok "no duplicate addresses" || fail "duplicate addresses"

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
# the headset shows 90 Hz at most: a higher FPS setting (flat Moonlight, 144 or 360 Hz screens) is clamped
setconf width=2560 height=1440 fps=144 bitrate=80000 videocfg=0
out="$(dry "DESKTOP-EXAMPLE:Desktop")"; echo "1440p144 setting: $out"
expect "fps 144 clamped to 90 in the hand-off" "$out" "--mode 2560x1440x90 --bitrate 80000"
refute "no mode above 90 fps" "$out" "1440x144 "
setconf width=2560 height=1440 fps=72 bitrate=80000 videocfg=0
out="$(dry "DESKTOP-EXAMPLE:Desktop")"
expect "72 fps setting stays 72" "$out" "--mode 2560x1440x72"
setconf width=2560 height=1440 fps=90 bitrate=80000 videocfg=0
setconf videocfg=4
out="$(dry "DESKTOP-EXAMPLE:Desktop")"; refute "AV1 choice: not passed" "$out" "--h264"
setconf videocfg=0 videodec=2 audiocfg=1 packetsize=1024
out="$(dry "DESKTOP-EXAMPLE:Desktop")"; echo "sw/5.1/pkt:    $out"
expect "software decode" "$out" "--sw-decode"; expect "5.1" "$out" "--audio 51"; expect "packet size" "$out" "--packet-size 1024"
setconf videodec=0 audiocfg=2 packetsize=0
out="$(dry "DESKTOP-EXAMPLE:Desktop")"; expect "7.1" "$out" "--audio 71"; refute "no packet size" "$out" "--packet-size"
setconf audiocfg=0
# the network path: the launcher's networkPath setting (auto, home = Wi-Fi, adapter = Steam Frame Wireless Adapter)
out="$(dry "DESKTOP-EXAMPLE:Desktop")"; expect "network default: auto" "$out" "--network auto"
setconf networkPath=adapter
out="$(dry "DESKTOP-EXAMPLE:Desktop")"; expect "network adapter" "$out" "--network adapter"
setconf networkPath=home
out="$(dry "DESKTOP-EXAMPLE:Desktop")"; expect "network home" "$out" "--network home"
setconf networkPath=bogus
out="$(dry "DESKTOP-EXAMPLE:Desktop")"; refute "unknown network value: not passed" "$out" "--network"
setconf networkPath=auto
out="$(dry "DESKTOP-EXAMPLE:Assassin's Creed Origins")"; expect "app by name -> id" "$out" "--app 1439608285"
out="$(MLXR_LAUNCHER_EXTRA_ARGS="--seconds 5" dry "DESKTOP-EXAMPLE:Desktop")"; expect "extra args" "$out" "--seconds 5"
grep -q '^width=1920' "$CONF" && fail "reseeded over the user's settings" || ok "later runs do not reseed"

# --- 3b. lane artemis-settings: the Artemis-style page's settings reach the VR client's command line
out="$(dry "DESKTOP-EXAMPLE:Desktop")"
expect "Refresh rate: Match headset by default" "$out" "--fps match"
SECTION=mlxr setconf match_headset_fps=false
setconf fps=60
out="$(dry "DESKTOP-EXAMPLE:Desktop")"
expect "Refresh rate 60 Hz: the number in --mode" "$out" "--mode 2560x1440x60"
refute "Refresh rate 60 Hz: no --fps match" "$out" "--fps match"
SECTION=mlxr setconf match_headset_fps=true
setconf fps=90
out="$(dry "DESKTOP-EXAMPLE:Desktop")"
for f in --pacing --video-priority --virtual-display --scale-factor --refresh-rate --full-range --no-rumble --deadzone \
         --no-mouse-nav-buttons --clipboard-sync --no-sops --local-audio; do
    refute "defaults: no $f" "$out" "$f"
done
setconf gameopts=false hostaudio=true
SECTION=mlxr setconf frame_pacing=latency ultra_low_latency=true virtual_display=true scale_factor=150 \
    custom_refresh_rate=59.94 full_range=true rumble=false deadzone=-5 mouse_nav_buttons=false clipboard_sync=true
out="$(dry "DESKTOP-EXAMPLE:Desktop")"; echo "artemis set:   $out"
expect "host: Optimize game settings off" "$out" "--no-sops"
expect "host: Play audio on PC" "$out" "--local-audio"
expect "frame pacing: lowest latency" "$out" "--pacing latency"
expect "ultra low latency: realtime threads" "$out" "--video-priority rt"
expect "Apollo virtual display" "$out" "--virtual-display"
expect "Apollo resolution scale factor" "$out" "--scale-factor 150"
expect "Apollo custom refresh rate" "$out" "--refresh-rate 59.94"
expect "full range" "$out" "--full-range"
expect "rumble off" "$out" "--no-rumble"
expect "dead zone" "$out" "--deadzone -5"
expect "mouse back/forward off" "$out" "--no-mouse-nav-buttons"
expect "clipboard sync" "$out" "--clipboard-sync"
case "$out" in *"--clipboard-sync --network auto --exit-to-caller"*) ok "flags before --exit-to-caller";; *) fail "flag order: $out";; esac
SECTION=mlxr setconf frame_pacing=smoothness custom_refresh_rate=144 scale_factor=100 deadzone=0 clipboard_sync=false
out="$(dry "DESKTOP-EXAMPLE:Desktop")"
expect "frame pacing: smoothest" "$out" "--pacing smoothness"
expect "custom refresh rate capped at the headset's 90 Hz" "$out" "--refresh-rate 90"
refute "a custom rate replaces Match headset" "$out" "--fps match"
refute "scale factor 100: not passed" "$out" "--scale-factor"
SECTION=mlxr setconf frame_pacing=balanced ultra_low_latency=false virtual_display=false custom_refresh_rate= \
    full_range=false rumble=true mouse_nav_buttons=true
setconf gameopts=true hostaudio=false
out="$(dry "DESKTOP-EXAMPLE:Desktop")"
expect "all back to defaults" "$out" "--bitrate 80000 --fps match --network auto --exit-to-caller"

# Smart Clipboard Sync: the clipboard goes to the VR client in a file, what it brings back is put on the clipboard
SECTION=mlxr setconf clipboard_sync=true
printf 'from the PC' > "$MLXR_CONFIG_DIR/clipboard-from-host.txt"
out="$(MLXR_LAUNCHER_TEST_CLIPBOARD="from the Frame" MLXR_LAUNCHER_DRYRUN=1 MLXR_NATIVE_CLIENT= selftest "DESKTOP-EXAMPLE:Desktop" 2>&1)"
expect "clipboard: the dry run removed the stale answer first" "$out" "nothing came back from the host"
cat >"$t/clip-client.sh" <<'CLIP'
#!/bin/bash
echo "to-host: [$(cat "$MLXR_CONFIG_DIR/clipboard-to-host.txt")] mode $(stat -c %a "$MLXR_CONFIG_DIR/clipboard-to-host.txt")" >> "$FAKE_LOG"
printf 'copied on the PC' > "$MLXR_CONFIG_DIR/clipboard-from-host.txt"
exit 0
CLIP
chmod +x "$t/clip-client.sh"
FAKE_LOG="$t/clip.log" MLXR_LAUNCHER_TEST_CLIPBOARD="from the Frame" MLXR_NATIVE_CLIENT="$t/clip-client.sh" selftest "DESKTOP-EXAMPLE:Desktop" >"$t/stdout"
expect "clipboard: the VR client finds the Frame's clipboard (0600)" "$(cat "$t/clip.log")" "to-host: [from the Frame] mode 600"
expect "clipboard: the PC's clipboard comes back" "$(cat "$t/stdout")" "took 16 bytes from the host"
[ ! -e "$MLXR_CONFIG_DIR/clipboard-to-host.txt" ] && [ ! -e "$MLXR_CONFIG_DIR/clipboard-from-host.txt" ] && ok "clipboard files removed after the stream" || fail "clipboard files left behind"
SECTION=mlxr setconf clipboard_sync=false

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
expect "client got the command line" "$(cat "$FAKE_LOG")" "--host $HOSTID --app 881448767 --mode 2560x1440x90 --bitrate 80000 --fps match --network auto --exit-to-caller"
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
    MLXR_LAUNCHER_IW_FILE="$here/iw-dev-sample.txt" MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_PAGE=settings MLXR_LAUNCHER_SCREENSHOT="$SHOTS/settings.png@3000" "$L" >"$t/ui3.out" 2>&1
    grep -Eq "$QMLERR" "$t/ui3.out" && { fail "settings QML errors"; grep -E "$QMLERR" "$t/ui3.out" | head; } || ok "settings page loads in hand-off mode (no QML errors)"
    # lane artemis-settings: hand-off mode shows the Artemis-style page, stock mode the stock one
    grep -q "MLXR_LAUNCHER_PAGE: qrc:/gui/XrSettingsView.qml" "$t/ui3.out" && ok "hand-off mode: Artemis-style Settings page" || fail "not the Artemis-style page: $(grep MLXR_LAUNCHER_PAGE "$t/ui3.out")"
    grep -q "Screenshot.*settings.png.*saved" "$t/ui3.out" && ok "settings page rendered" || fail "no settings screenshot"
    grep -q "MLXR_LAUNCHER_WIFI: Wi-Fi (Example Net 5G, 5 GHz)" "$t/ui3.out" && ok "Network connection: the managed Wi-Fi's name and band (not the AP)" || fail "wifi label: $(grep MLXR_LAUNCHER_WIFI "$t/ui3.out")"
    MLXR_HANDOFF=0 MLXR_LAUNCHER_PAGE=settings MLXR_LAUNCHER_SCREENSHOT="$t/stock-settings.png@3000" "$L" >"$t/ui5.out" 2>&1
    grep -q "MLXR_LAUNCHER_PAGE: qrc:/gui/SettingsView.qml" "$t/ui5.out" && ok "hand-off off: stock Settings page" || fail "stock page missing with MLXR_HANDOFF=0"
    # the page edits the VR client's config.ini: only the keys changed, the rest byte for byte
    ini="$MLXR_CONFIG_DIR/config.ini"
    printf '# Moonlight XR settings\n[display]\n# Display tab: show performance stats [true | false] (default false)\n#stats = false\nglow = true\n\n[3d]\nseparation = 9\nstereo = false\n' > "$ini"
    cp "$ini" "$t/ini.before"
    MLXR_LAUNCHER_IW_FILE=/dev/null MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_PAGE=settings MLXR_LAUNCHER_CLICK=statsRow,flipRow,hdrRow \
        MLXR_LAUNCHER_SCREENSHOT="$SHOTS/settings-clicked.png@3000" "$L" >"$t/ui6.out" 2>&1
    grep -q "no clickable" "$t/ui6.out" && fail "click hook: $(grep 'no clickable' "$t/ui6.out")"
    grep -q '^stats = true$' "$ini" && ok "stats overlay row writes [display] stats = true" || { fail "stats not written"; cat "$ini"; }
    grep -q '^#stats' "$ini" && fail "commented stats line left as well" || ok "the commented default line was replaced"
    grep -A1 '^\[vr\]' "$ini" | grep -q '^flip_face_buttons = true$' && ok "flip row adds [vr] flip_face_buttons = true" || { fail "flip not written"; cat "$ini"; }
    grep -q '^glow = true$' "$ini" && grep -q '^separation = 9$' "$ini" && grep -q '^stereo = false$' "$ini" && ok "other config.ini keys untouched" || { fail "config.ini keys changed"; diff "$t/ini.before" "$ini"; }
    grep -q -i hdr "$ini" && fail "greyed HDR row changed something" || ok "greyed HDR row is inert"
    grep -q "MLXR_LAUNCHER_WIFI: Wi-Fi$" "$t/ui6.out" && ok "Network connection: plain Wi-Fi when unknown" || fail "wifi fallback: $(grep MLXR_LAUNCHER_WIFI "$t/ui6.out")"
    # what the VR client's own parser makes of it
    if [ -n "${MLXR_SETTINGS_DUMP:-}" ] && [ -x "$MLXR_SETTINGS_DUMP" ]; then
        dump="$("$MLXR_SETTINGS_DUMP" "$ini")"
        expect "VR client reads stats" "$dump" "display.stats = true"
        expect "VR client reads flip" "$dump" "vr.flip_face_buttons = true"
    fi
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
