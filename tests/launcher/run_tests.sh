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
# lane wave4-launcher: the check before a stream asks the PC; only section 3e turns it on, against a fake PC
export MLXR_LAUNCHER_PREFLIGHT=0
unset MLXR_LAUNCHER_PREFLIGHT_URL MLXR_APP_UUID
# lane launcher-bundle: the update check asks GitHub; only section 3f turns it on, with the answer injected or from a
# local fake. On start is off unless a test sets it.
export MLXR_LAUNCHER_UPDATE=0
unset MLXR_LAUNCHER_UPDATE_JSON MLXR_LAUNCHER_UPDATE_URL MLXR_LAUNCHER_ONSTART_ONLINE MLXR_LAUNCHER_ONSTART_WAIT_MS
mkdir -p "$HOME" "$MLXR_LOG_DIR"
# Lane refresh: SteamVR's per-app refresh rate is set through vrcmd. The tests never reach the real
# one (it would change this machine's SteamVR settings): a fake records its arguments instead, and
# no SteamAppId is inherited.
unset SteamAppId SteamGameId
cat >"$t/fake-vrcmd" <<'VRCMD'
#!/bin/bash
echo "$*" >> "$FAKE_VRCMD_LOG"
exit "${FAKE_VRCMD_EXIT:-0}"
VRCMD
chmod +x "$t/fake-vrcmd"
export MLXR_VRCMD="$t/fake-vrcmd" FAKE_VRCMD_LOG="$t/vrcmd.log"
: > "$FAKE_VRCMD_LOG"
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
sed -i 's/100\.64\.0\.5/198.51.100.5/; s/203\.0\.113\.7/198.51.100.7/; s/192\.168\.1\.77/198.51.100.77/' "$t/Moonlight.conf"
cp "$t/Moonlight.conf" "$t/Moonlight.conf.orig"
export MLXR_SEED_CONF="$t/Moonlight.conf"
HOSTID=6E1F0C2A-3B4D-4E5F-8A9B-0C1D2E3F4A5B
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
out="$(dry "gaming-pc:Desktop")"
echo "first run: $out"
[ -f "$CONF" ] && ok "launcher config created at $CONF" || fail "launcher config missing"
cmp -s "$t/Moonlight.conf" "$t/Moonlight.conf.orig" && ok "source config unmodified" || fail "source config modified"
grep -q 'gaming-pc' "$CONF" && grep -q '6E1F0C2A' "$CONF" && ok "seeded PC list is in the launcher config" || fail "hosts not copied"
grep -q '^width=2560' "$CONF" && grep -q '^fps=90' "$CONF" && grep -q '^bitrate=98000' "$CONF" && ok "stream settings set for the headset" || fail "VR defaults not applied"
grep -q '^certificate=' "$CONF" && ok "identity copied" || fail "identity not copied"
expect "1440p90 98 Mbps, HEVC (no --h264)" "$out" "--host $HOSTID --app 881448767 --mode 2560x1440x90 --bitrate 98000 --fps match --network auto --exit-to-caller"
refute "no h264 flag by default" "$out" "--h264"
case "$out" in *moonlight-xr\ --host*) ok "program is moonlight-xr";; *) fail "program";; esac

# --- 2. the pairing the native client gets
cmp "$t/qtclient/client.pem" "$MLXR_CONFIG_DIR/client.pem" && ok "client.pem is the launcher's identity" || fail "client.pem"
cmp "$t/qtclient/client.key" "$MLXR_CONFIG_DIR/client.key" && ok "client.key is the launcher's identity" || fail "client.key"
cmp "$t/fakesrv/client.pem" "$MLXR_CONFIG_DIR/servercerts/$HOSTID.pem" && ok "pinned server cert shared" || fail "server cert"
[ "$(stat -c %a "$MLXR_CONFIG_DIR/client.key")" = 600 ] && [ "$(stat -c %a "$MLXR_CONFIG_DIR/hosts.ini")" = 600 ] && ok "private files are 0600" || fail "modes"
h="$MLXR_CONFIG_DIR/hosts.ini"
grep -q "^\[$HOSTID\]" "$h" && grep -q '^name = gaming-pc' "$h" && grep -q '^mac = 02:f0:2c:5d:a5:10' "$h" && grep -q '^cert = servercerts/' "$h" && ok "hosts.ini record" || { fail "hosts.ini"; cat "$h"; }
grep -q '^address = 198.51.100.5' "$h" && grep -q 'extra_addresses = .*198.51.100.7' "$h" && ok "addresses (no loopback)" || { fail "addresses"; cat "$h"; }
"$CLI" identity 2>&1 | grep -q "saved hosts:  1" && ok "mlxr-cli reads the exported host" || fail "mlxr-cli: $("$CLI" identity 2>&1)"
"$CLI" identity 2>&1 | grep -q "created a new client identity" && fail "identity regenerated"
# another host already in hosts.ini is kept
printf '\n[OTHER-UUID]\nname = other\naddress = 10.9.9.9\n' >> "$h"
dry "gaming-pc:Desktop" >/dev/null
grep -q '^\[OTHER-UUID\]' "$h" && [ "$(grep -c "^\[$HOSTID\]" "$h")" = 1 ] && ok "other hosts kept, no duplicates" || fail "hosts.ini merge"
# an address the PC was known by (another network path) survives the next hand-off, so both paths stay candidates
sed -i "/^\[$HOSTID\]/,/^cert/ s/^extra_addresses = .*/&,10.35.50.27/" "$h"
grep -q 'extra_addresses = .*10.35.50.27' "$h" || fail "test setup: extra address not added"
dry "gaming-pc:Desktop" >/dev/null
grep -q '^address = 198.51.100.5' "$h" && grep -q 'extra_addresses = .*198.51.100.7' "$h" && grep -q 'extra_addresses = .*10.35.50.27' "$h" && ok "earlier addresses kept across hand-offs" || { fail "address merge"; cat "$h"; }
[ "$(grep -o '198.51.100.7' "$h" | wc -l)" = 1 ] && ok "no duplicate addresses" || fail "duplicate addresses"

# --- 3. the settings matrix (user changes them in Settings; here in the conf, as Settings would)
setconf width=1920 height=1080 fps=60 bitrate=20000 videocfg=0
out="$(dry "gaming-pc:Desktop")"; echo "1080p60 auto:  $out"
expect "1080p60" "$out" "--mode 1920x1080x60 --bitrate 20000"
refute "auto codec: no --h264" "$out" "--h264"
setconf width=2560 height=1440 fps=90 bitrate=80000 videocfg=1
out="$(dry "gaming-pc:Desktop")"; echo "1440p90 H.264: $out"
expect "1440p90" "$out" "--mode 2560x1440x90 --bitrate 80000"
expect "forced H.264" "$out" "--h264"
# lane wave4b-abr: a saved 400 Mbps (the Video bitrate row now goes to 500) reaches the client unchanged, whatever the resolution
setconf width=2560 height=1440 fps=90 bitrate=400000 videocfg=0
out="$(dry "gaming-pc:Desktop")"; expect "saved 400 Mbps at 1440p90" "$out" "--mode 2560x1440x90 --bitrate 400000"
setconf width=1920 height=1080 fps=60
out="$(dry "gaming-pc:Desktop")"; expect "saved 400 Mbps survives a resolution change (1080p60)" "$out" "--mode 1920x1080x60 --bitrate 400000"
setconf width=3840 height=2160 fps=90 bitrate=500000
out="$(dry "gaming-pc:Desktop")"; expect "saved 500 Mbps at 4K" "$out" "--mode 3840x2160x90 --bitrate 500000"
setconf width=2560 height=1440 fps=90 bitrate=80000 videocfg=1
setconf videocfg=2
out="$(dry "gaming-pc:Desktop")"; refute "forced HEVC: no --h264" "$out" "--h264"
# the headset shows 144 Hz at most (lane refresh; was 90): a higher FPS setting (flat Moonlight on
# a 165 or 360 Hz screen) is clamped; the VR client caps it again at the rate SteamVR reports
setconf width=2560 height=1440 fps=165 bitrate=80000 videocfg=0
out="$(dry "gaming-pc:Desktop")"; echo "1440p165 setting: $out"
expect "fps 165 clamped to 144 in the hand-off" "$out" "--mode 2560x1440x144 --bitrate 80000"
refute "no mode above 144 fps" "$out" "1440x165 "
setconf width=2560 height=1440 fps=144 bitrate=80000 videocfg=0
out="$(dry "gaming-pc:Desktop")"
expect "144 fps setting stays 144" "$out" "--mode 2560x1440x144"
setconf width=2560 height=1440 fps=120 bitrate=80000 videocfg=0
out="$(dry "gaming-pc:Desktop")"
expect "120 fps setting stays 120" "$out" "--mode 2560x1440x120"
setconf width=2560 height=1440 fps=72 bitrate=80000 videocfg=0
out="$(dry "gaming-pc:Desktop")"
expect "72 fps setting stays 72" "$out" "--mode 2560x1440x72"
setconf width=2560 height=1440 fps=90 bitrate=80000 videocfg=0
setconf videocfg=4
out="$(dry "gaming-pc:Desktop")"; refute "AV1 choice: not passed" "$out" "--h264"
setconf videocfg=0 videodec=2 audiocfg=1 packetsize=1024
out="$(dry "gaming-pc:Desktop")"; echo "sw/5.1/pkt:    $out"
expect "software decode" "$out" "--sw-decode"; expect "5.1" "$out" "--audio 51"; expect "packet size" "$out" "--packet-size 1024"
setconf videodec=0 audiocfg=2 packetsize=0
out="$(dry "gaming-pc:Desktop")"; expect "7.1" "$out" "--audio 71"; refute "no packet size" "$out" "--packet-size"
setconf audiocfg=0
# the network path: the launcher's networkPath setting (auto, home = Wi-Fi, adapter = Steam Frame Wireless Adapter, usb = USB cable)
out="$(dry "gaming-pc:Desktop")"; expect "network default: auto" "$out" "--network auto"
setconf networkPath=adapter
out="$(dry "gaming-pc:Desktop")"; expect "network adapter" "$out" "--network adapter"
setconf networkPath=home
out="$(dry "gaming-pc:Desktop")"; expect "network home" "$out" "--network home"
setconf networkPath=usb
out="$(dry "gaming-pc:Desktop")"; expect "network usb" "$out" "--network usb"
setconf networkPath=bogus
out="$(dry "gaming-pc:Desktop")"; refute "unknown network value: not passed" "$out" "--network"
setconf networkPath=auto
out="$(dry "gaming-pc:Assassin's Creed Origins")"; expect "app by name -> id" "$out" "--app 1439608285"
out="$(MLXR_LAUNCHER_EXTRA_ARGS="--seconds 5" dry "gaming-pc:Desktop")"; expect "extra args" "$out" "--seconds 5"
grep -q '^width=1920' "$CONF" && fail "reseeded over the user's settings" || ok "later runs do not reseed"

# --- 3b. lane artemis-settings: the Artemis-style page's settings reach the VR client's command line
out="$(dry "gaming-pc:Desktop")"
expect "Refresh rate: Match headset by default" "$out" "--fps match"
SECTION=mlxr setconf match_headset_fps=false
setconf fps=60
out="$(dry "gaming-pc:Desktop")"
expect "Refresh rate 60 Hz: the number in --mode" "$out" "--mode 2560x1440x60"
refute "Refresh rate 60 Hz: no --fps match" "$out" "--fps match"
SECTION=mlxr setconf match_headset_fps=true
setconf fps=90
out="$(dry "gaming-pc:Desktop")"
for f in --pacing --video-priority --virtual-display --scale-factor --refresh-rate --full-range --no-rumble --deadzone \
         --no-mouse-nav-buttons --clipboard-sync --no-sops --local-audio; do
    refute "defaults: no $f" "$out" "$f"
done
setconf gameopts=false hostaudio=true
SECTION=mlxr setconf frame_pacing=latency ultra_low_latency=true virtual_display=true scale_factor=150 \
    custom_refresh_rate=59.94 full_range=true rumble=false deadzone=-5 mouse_nav_buttons=false clipboard_sync=true
out="$(dry "gaming-pc:Desktop")"; echo "artemis set:   $out"
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
out="$(dry "gaming-pc:Desktop")"
expect "frame pacing: smoothest" "$out" "--pacing smoothness"
expect "custom refresh rate 144 kept (lane refresh)" "$out" "--refresh-rate 144"
refute "a custom rate replaces Match headset" "$out" "--fps match"
SECTION=mlxr setconf custom_refresh_rate=200
out="$(dry "gaming-pc:Desktop")"
expect "custom refresh rate capped at the headset's 144 Hz" "$out" "--refresh-rate 144"
refute "scale factor 100: not passed" "$out" "--scale-factor"
SECTION=mlxr setconf frame_pacing=balanced ultra_low_latency=false virtual_display=false custom_refresh_rate= \
    full_range=false rumble=true mouse_nav_buttons=true
setconf gameopts=true hostaudio=false
out="$(dry "gaming-pc:Desktop")"
expect "all back to defaults" "$out" "--bitrate 80000 --fps match --network auto --exit-to-caller"

# --- 3c. lane refresh: a display rate picked on the page (72, 90, 120, 144) is also set as SteamVR's
# per-app rate through vrcmd before the client starts; Match headset, 60/30 and a custom rate leave
# it. A synthetic appid; the fake vrcmd from the top records the call.
APPID=1234567890
vr() { # SteamAppId value, spec -> the command line; the vrcmd log and stderr are left for the checks
    : > "$FAKE_VRCMD_LOG"
    if [ -n "$1" ]; then SteamAppId="$1" dry "$2"; else dry "$2"; fi
}
SECTION=mlxr setconf match_headset_fps=false custom_refresh_rate=
for r in 144 120 90 72; do
    setconf fps=$r
    out="$(vr $APPID "gaming-pc:Desktop")"
    expect "fixed $r Hz: --mode carries it" "$out" "--mode 2560x1440x$r"
    expect "fixed $r Hz: vrcmd sets SteamVR's per-app rate" "$(cat "$FAKE_VRCMD_LOG")" \
        "--set-settings-int steam.app.$APPID.preferredRefreshRate $r"
    expect "fixed $r Hz: logged" "$(cat "$t/stderr")" "steam.app.$APPID.preferredRefreshRate set to $r Hz (vrcmd)"
done
setconf fps=60
out="$(vr $APPID "gaming-pc:Desktop")"
[ ! -s "$FAKE_VRCMD_LOG" ] && ok "60 fps: SteamVR's rate left alone" || fail "60 fps called vrcmd: $(cat "$FAKE_VRCMD_LOG")"
setconf fps=120
SECTION=mlxr setconf custom_refresh_rate=119.88
out="$(vr $APPID "gaming-pc:Desktop")"
[ ! -s "$FAKE_VRCMD_LOG" ] && ok "custom rate: SteamVR's rate left alone" || fail "custom rate called vrcmd"
SECTION=mlxr setconf custom_refresh_rate= match_headset_fps=true
out="$(vr $APPID "gaming-pc:Desktop")"
expect "Match headset: --fps match" "$out" "--fps match"
[ ! -s "$FAKE_VRCMD_LOG" ] && ok "Match headset: SteamVR's rate left alone" || fail "Match headset called vrcmd"
SECTION=mlxr setconf match_headset_fps=false
out="$(vr "" "gaming-pc:Desktop")"
[ ! -s "$FAKE_VRCMD_LOG" ] && ok "no SteamAppId: vrcmd not called" || fail "vrcmd called without an appid"
expect "no SteamAppId: logged" "$(cat "$t/stderr")" "120 Hz not set, the launcher was not started from its Steam shortcut"
expect "no SteamAppId: the stream still starts" "$out" "--mode 2560x1440x120"
out="$(FAKE_VRCMD_EXIT=3 vr $APPID "gaming-pc:Desktop")"
expect "vrcmd failing: logged" "$(cat "$t/stderr")" "120 Hz not set, vrcmd failed (exit 3)"
expect "vrcmd failing: the stream still starts" "$out" "--mode 2560x1440x120"
out="$(MLXR_VRCMD="$t/no-such-vrcmd" vr $APPID "gaming-pc:Desktop")"
expect "vrcmd missing: logged" "$(cat "$t/stderr")" "120 Hz not set, no vrcmd at"
expect "vrcmd missing: the stream still starts" "$out" "--mode 2560x1440x120"
SECTION=mlxr setconf match_headset_fps=true
setconf fps=90

# Smart Clipboard Sync: the clipboard goes to the VR client in a file, what it brings back is put on the clipboard
SECTION=mlxr setconf clipboard_sync=true
printf 'from the PC' > "$MLXR_CONFIG_DIR/clipboard-from-host.txt"
out="$(MLXR_LAUNCHER_TEST_CLIPBOARD="from the Frame" MLXR_LAUNCHER_DRYRUN=1 MLXR_NATIVE_CLIENT= selftest "gaming-pc:Desktop" 2>&1)"
expect "clipboard: the dry run removed the stale answer first" "$out" "nothing came back from the host"
cat >"$t/clip-client.sh" <<'CLIP'
#!/bin/bash
echo "to-host: [$(cat "$MLXR_CONFIG_DIR/clipboard-to-host.txt")] mode $(stat -c %a "$MLXR_CONFIG_DIR/clipboard-to-host.txt")" >> "$FAKE_LOG"
printf 'copied on the PC' > "$MLXR_CONFIG_DIR/clipboard-from-host.txt"
exit 0
CLIP
chmod +x "$t/clip-client.sh"
FAKE_LOG="$t/clip.log" MLXR_LAUNCHER_TEST_CLIPBOARD="from the Frame" MLXR_NATIVE_CLIENT="$t/clip-client.sh" selftest "gaming-pc:Desktop" >"$t/stdout"
expect "clipboard: the VR client finds the Frame's clipboard (0600)" "$(cat "$t/clip.log")" "to-host: [from the Frame] mode 600"
expect "clipboard: the PC's clipboard comes back" "$(cat "$t/stdout")" "took 16 bytes from the host"
[ ! -e "$MLXR_CONFIG_DIR/clipboard-to-host.txt" ] && [ ! -e "$MLXR_CONFIG_DIR/clipboard-from-host.txt" ] && ok "clipboard files removed after the stream" || fail "clipboard files left behind"
SECTION=mlxr setconf clipboard_sync=false

# --- 3c. lane launcher2: the default bitrate (hand-off mode) and the Game Mode UI scale
# Twice stock's getDefaultBitrate (same table, same square-root frame-rate factor), rounded to
# 500 kbps, capped at the slider's maximum (150000). The dry-run variable makes the hand-off count
# as active; MLXR_HANDOFF=0 gives stock's own figure.
bitrate() { # WxHxFPS[x444] -> the default bitrate in kbps
    MLXR_LAUNCHER_DRYRUN=1 MLXR_LAUNCHER_SELFTEST="bitrate:$1" "$L" 2>/dev/null | sed -n 's/^SELFTEST: bitrate=\([0-9]*\) .*/\1/p'
}
# lane refresh: 1440p120 and 1440p144 from the same rule (stock's square root of the rate over 60)
for c in 1920x1080x60:40000 1920x1080x90:49000 2560x1440x60:80000 2560x1440x72:87500 2560x1440x90:98000 \
         2560x1440x120:113000 2560x1440x144:124000 3840x2160x60:150000 3840x2160x90:150000 2560x1440x90x444:150000; do
    got="$(bitrate "${c%%:*}")"
    [ "$got" = "${c##*:}" ] && ok "default bitrate ${c%%:*} = ${c##*:} kbps" || fail "default bitrate ${c%%:*}: wanted ${c##*:}, got [$got]"
done
# hand-off off: stock's figure (1440p90 rounds to 49000, 1080p60 to 20000)
for c in 2560x1440x90:49000 1920x1080x60:20000 2560x1440x120:57000 2560x1440x144:62000; do
    got="$(MLXR_HANDOFF=0 MLXR_LAUNCHER_SELFTEST="bitrate:${c%%:*}" "$L" 2>/dev/null | sed -n 's/^SELFTEST: bitrate=\([0-9]*\) .*/\1/p')"
    [ "$got" = "${c##*:}" ] && ok "hand-off off: stock bitrate ${c%%:*} = ${c##*:} kbps" || fail "stock bitrate ${c%%:*}: wanted ${c##*:}, got [$got]"
done
# the first-run seed (section 1: bitrate=98000) is the same function's 1440p90 value
[ "$(bitrate 2560x1440x90)" = 98000 ] && ok "first-run seed value matches the rule" || fail "seed and rule disagree"
scale() { # [VAR=value ...] -> "scale=X QT_SCALE_FACTOR=Y"
    env -u QT_SCALE_FACTOR -u MLXR_LAUNCHER_SCALE "$@" MLXR_LAUNCHER_SELFTEST=scale "$L" 2>/dev/null | sed -n 's/^SELFTEST: //p'
}
expect "Game Mode: scale 1.5 by default" "$(scale XDG_CURRENT_DESKTOP=gamescope)" "scale=1.5 QT_SCALE_FACTOR=1.5"
expect "Game Mode: MLXR_LAUNCHER_SCALE=2" "$(scale XDG_CURRENT_DESKTOP=gamescope MLXR_LAUNCHER_SCALE=2)" "scale=2 QT_SCALE_FACTOR=2"
expect "Game Mode: scale clamped to 2.5" "$(scale XDG_CURRENT_DESKTOP=gamescope MLXR_LAUNCHER_SCALE=9)" "scale=2.5 QT_SCALE_FACTOR=2.5"
expect "Game Mode: scale clamped to 1.0" "$(scale XDG_CURRENT_DESKTOP=gamescope MLXR_LAUNCHER_SCALE=0.3)" "scale=1 QT_SCALE_FACTOR=1"
expect "Game Mode: junk scale falls back to 1.5" "$(scale XDG_CURRENT_DESKTOP=gamescope MLXR_LAUNCHER_SCALE=big)" "scale=1.5 QT_SCALE_FACTOR=1.5"
expect "desktop: stock, QT_SCALE_FACTOR untouched" "$(scale XDG_CURRENT_DESKTOP=KDE MLXR_LAUNCHER_SCALE=2)" "scale=1 QT_SCALE_FACTOR="
refute "desktop: no QT_SCALE_FACTOR set" "$(scale XDG_CURRENT_DESKTOP=KDE MLXR_LAUNCHER_SCALE=2)" "QT_SCALE_FACTOR=2"
expect "Game Mode: the owner's QT_SCALE_FACTOR is kept" "$(env XDG_CURRENT_DESKTOP=gamescope QT_SCALE_FACTOR=1.25 MLXR_LAUNCHER_SELFTEST=scale "$L" 2>/dev/null)" "QT_SCALE_FACTOR=1.25"

# Opening a page for the headset's user (patch 0014, lane wave6-kofi): in Game Mode with steam on PATH it is
# `steam steam://openurl/<url>`, else the normal openUrl. The self-test prints the choice without running it.
kofi_url="https://ko-fi.com/moonlightxr"
fakebin="$(mktemp -d)"; printf '#!/bin/sh\nexit 0\n' > "$fakebin/steam"; chmod +x "$fakebin/steam"
nobin="$(mktemp -d)"
openurl() { # PATHDIR DESKTOP URL -> "steam steam://openurl/URL" or "qt URL"
    env PATH="$1:/usr/bin:/bin" XDG_CURRENT_DESKTOP="$2" MLXR_LAUNCHER_SELFTEST="openurl:$3" "$L" 2>/dev/null | sed -n 's/^SELFTEST: openurl=//p'
}
if command -v steam >/dev/null 2>&1; then
    echo "skip: a real steam is on this PATH (openurl checks)"
else
    expect "Game Mode with steam: the Steam URL" "$(openurl "$fakebin" gamescope "$kofi_url")" "steam steam://openurl/$kofi_url"
    expect "Game Mode without steam: the normal openUrl" "$(openurl "$nobin" gamescope "$kofi_url")" "qt $kofi_url"
    expect "desktop with steam: the normal openUrl" "$(openurl "$fakebin" KDE "$kofi_url")" "qt $kofi_url"
    expect "Game Mode: a non-web address is not handed to steam" "$(openurl "$fakebin" gamescope "file:///etc/passwd")" "qt file:///etc/passwd"
fi
rm -rf "$fakebin" "$nobin"

# The Help button (patch 0011): this app's readme in hand-off mode, stock's Setup-Guide otherwise. The QML is
# compiled into the binary, so this reads the patched main.qml that launcher/build.sh left next to it.
mq="$(dirname "$L")/src/app/gui/main.qml"
if [ -f "$mq" ]; then
    grep -q 'SystemProperties.nativeVrHandoff ?' "$mq" && grep -q 'SystemProperties.openExternalUrl("https://github.com/simplysanatori/moonlight-xr-steam-frame#readme") :' "$mq" \
        && grep -q 'Qt.openUrlExternally("https://github.com/moonlight-stream/moonlight-docs/wiki/Setup-Guide");' "$mq" \
        && ok "Help: readme in hand-off mode, stock Setup-Guide otherwise" || fail "Help button links"
    [ "$(grep -c 'moonlight-docs/wiki/Fixing-Hardware-Decoding-Problems' "$mq")" = 2 ] && ok "Help: the other stock links are unchanged" || fail "other stock links changed"
else
    echo "skip: no $mq (Help button check)"
fi

# --- 3d. lane wave4-launcher: settings profiles per PC or app (PLAN 4.7). A profile is a saved copy of the
# stream settings; the one for the PC or the app is in force while makePlan builds the command line and until
# the stream ends, and nothing of it is saved over the Settings page's own values. profileops runs the
# MlxrProfiles calls the Stream profiles page makes; plan: prints the settings before, during and after a plan.
pops() { MLXR_LAUNCHER_SELFTEST="profileops:$1" "$L" 2>"$t/stderr"; }
pplan() { MLXR_LAUNCHER_DRYRUN=1 MLXR_NATIVE_CLIENT= MLXR_LAUNCHER_SELFTEST="plan:$1" "$L" 2>"$t/stderr"; }
setconf width=2560 height=1440 fps=90 bitrate=80000 videocfg=0
SECTION=mlxr setconf match_headset_fps=true virtual_display=false scale_factor=100 custom_refresh_rate=
out="$(pops 'set|width|1920~set|height|1080~set|fps|60~set|bitrateKbps|20000~set|matchHeadsetFps|false~create|Desktop~set|width|3840~set|height|2160~set|fps|120~set|bitrateKbps|150000~set|virtualDisplay|true~set|scaleFactor|150~set|perfOverlay|true~set|notices|off~set|depthScale|150~set|stereoSource|sbs~set|bitrateAdapter|60000~set|padGyro|controller~set|networkPath|adapter~create|Game~create|desktop~create|Default~create|~duplicate|Game~rename|Game copy|Cinema~rename|Nope|X~assign|gaming-pc||Desktop~assign|gaming-pc|Desktop|Game~assign|gaming-pc|Desktop|Nope~dump')"
echo "$out" | sed 's/^/    /'
expect "profiles: made from the settings in force" "$out" "create|Desktop -> ok"
expect "profiles: a second one" "$out" "create|Game -> ok"
expect "profiles: names are unique, case ignored" "$out" "create|desktop -> There is a profile called desktop already."
expect "profiles: Default is not a profile name" "$out" "create|Default -> Default is the settings on the Settings page"
expect "profiles: a name is needed" "$out" "create| -> A profile needs a name."
expect "profiles: duplicate" "$out" "duplicate|Game -> ok"
expect "profiles: rename" "$out" "rename|Game copy|Cinema -> ok"
expect "profiles: rename of a missing one" "$out" "rename|Nope|X -> There is no profile called Nope."
expect "profiles: an unknown profile is not assigned" "$out" "assign|gaming-pc|Desktop|Nope -> There is no profile called Nope."
expect "profiles: Desktop holds 1080p 60 Hz 20 Mbps" "$out" 'profile "Desktop": 1920x1080, 60 Hz, 20 Mbps'
expect "profiles: Game holds 4K 120 Hz, 150 Mbps, a virtual display at 150 %" "$out" 'profile "Game": 3840x2160, 120 Hz, 150 Mbps, 3D side by side, virtual display 150%'
expect "profiles: the copy keeps the values" "$out" 'profile "Cinema": 3840x2160, 120 Hz, 150 Mbps'
expect "profiles: the PC's every app uses Desktop" "$out" 'use "gaming-pc" / "" -> "Desktop"'
expect "profiles: one app uses Game" "$out" 'use "gaming-pc" / "Desktop" -> "Game"'
grep -q '^width=2560$' "$CONF" && grep -q '^fps=90$' "$CONF" && grep -q '^bitrate=80000$' "$CONF" \
    && ok "profiles: making them did not save over the Settings page's values" || fail "profiles: the page's values changed"
grep -q '^1\\name=Desktop$' "$CONF" && ok "profiles: kept in the launcher's settings" || fail "profiles: not in $CONF"
# a stream: the app's profile wins over the PC's; the config.ini keys it holds are written for the stream and put back
ini="$MLXR_CONFIG_DIR/config.ini"
printf '# Moonlight XR settings\n[display]\nstats = false\nglow = true\n\n[vr]\nnotices = all\n' > "$ini"
cp "$ini" "$t/ini.profile"
out="$(pplan "gaming-pc:Desktop")"
echo "$out" | sed 's/^/    /'
expect "profile for the app: Game" "$out" "SELFTEST: profile: Game"
expect "profile: the Settings page's values before" "$out" "before: width=2560 height=1440 fps=90 bitrateKbps=80000"
expect "profile: the profile's values during the stream" "$out" "during: width=3840 height=2160 fps=120 bitrateKbps=150000"
expect "profile: the command line has them" "$out" "--mode 3840x2160x120 --bitrate 150000"
expect "profile: virtual display and scale factor" "$out" "--virtual-display --scale-factor 150"
expect "profile: its network path" "$out" "--network adapter"
refute "profile: a fixed rate, no --fps match" "$out" "--fps match"
expect "profile: the Settings page's values after" "$out" "after: width=2560 height=1440 fps=90 bitrateKbps=80000"
expect "profile: the launcher settings back after" "$(echo "$out" | grep 'SELFTEST: after:')" "virtualDisplay=false scaleFactor=100"
expect "profile: the network path back after" "$(echo "$out" | grep 'SELFTEST: after:')" "networkPath=auto"
cmp -s "$ini" "$t/ini.profile" && ok "profile: config.ini as it was after the stream" || { fail "profile: config.ini not put back"; diff "$t/ini.profile" "$ini"; }
expect "profile: the config.ini keys are logged" "$(cat "$t/stderr")" "MLXR_LAUNCHER_PROFILE: using Game for gaming-pc / Desktop"
out="$(pplan "gaming-pc:Assassin's Creed Origins")"
expect "profile for the PC's other apps: Desktop" "$out" "SELFTEST: profile: Desktop"
expect "profile Desktop: 1080p60 20 Mbps" "$out" "--mode 1920x1080x60 --bitrate 20000"
expect "profile Desktop: the Settings page's network path" "$out" "--network auto"
out="$(dry "gaming-pc:Desktop")"
expect "profile: the hand-off's own run uses it too" "$out" "--mode 3840x2160x120 --bitrate 150000"
grep -q '^width=2560$' "$CONF" && grep -q '^virtual_display=false$' "$CONF" && ok "profile: nothing of it saved by a stream" || fail "profile: saved by the stream"
# a launcher killed during the stream: the config.ini keys go back at its next start, a key the headset changed is kept
out="$(MLXR_LAUNCHER_TEST_PROFILE_CRASH=1 pplan "gaming-pc:Desktop")"
grep -q '^stats = true$' "$ini" && grep -q '^notices = off$' "$ini" && grep -q '^depth_scale = 150$' "$ini" && grep -q '^stereo_source = sbs$' "$ini" \
    && grep -q '^bitrate_adapter = 60000$' "$ini" && grep -q '^pad_gyro = controller$' "$ini" \
    && ok "profile: config.ini has the profile's keys during the stream (3D depth and source, the adapter's bitrate, the pad gyro)" || { fail "profile keys not written"; cat "$ini"; }
out="$(pops 'dump')"
cmp -s "$ini" "$t/ini.profile" && ok "profile: a killed launcher's config.ini is put back byte for byte at the next start" || { fail "not recovered"; diff "$t/ini.profile" "$ini"; }
expect "profile: the recovery is logged" "$(cat "$t/stderr")" "put config.ini back as it was"
out="$(MLXR_LAUNCHER_TEST_PROFILE_CRASH=1 pplan "gaming-pc:Desktop")"
sed -i 's/^notices = off$/notices = important/' "$ini"
out="$(pops 'dump')"
grep -q '^stats = false$' "$ini" && ok "profile: after a headset change, the keys are put back one by one" || { fail "not recovered"; cat "$ini"; }
grep -q '^notices = important$' "$ini" && ok "profile: a key the headset changed meanwhile is kept" || { fail "headset's change lost"; cat "$ini"; }
grep -q '^depth_scale = 100$' "$ini" && ok "profile: a key the file did not have gets the value it was read as" || { fail "depth_scale"; cat "$ini"; }
expect "profile: the key recovery is logged" "$(cat "$t/stderr")" "put config.ini display stats back to false"
out="$(pops 'dump')"
refute "profile: recovered once only" "$(cat "$t/stderr")" "put config.ini"
cp "$t/ini.profile" "$ini"
# the app's own profile taken away: the PC's is used; a profile deleted: no profile
out="$(pops 'assign|gaming-pc|Desktop|Default~remove|Cinema~dump')"
expect "profiles: Default takes an app's profile away" "$out" "assign|gaming-pc|Desktop|Default -> ok"
refute "profiles: the app's line is gone" "$out" 'use "gaming-pc" / "Desktop"'
expect "profiles: delete" "$out" "remove|Cinema -> ok"
out="$(pplan "gaming-pc:Desktop")"
expect "profile: the PC's profile for the app now" "$out" "SELFTEST: profile: Desktop"
out="$(pops 'remove|Desktop~dump')"
refute "profiles: a deleted profile is taken from every PC and app" "$out" 'use "gaming-pc"'
out="$(pplan "gaming-pc:Desktop")"
expect "profile: none in force" "$out" "SELFTEST: profile: none"
expect "profile none: the Settings page's settings" "$out" "--mode 2560x1440x90 --bitrate 80000 --fps match --network auto"
# Load into the Settings page: the profile's values become the page's own (saved)
out="$(pops 'load|Game')"
grep -q '^width=3840$' "$CONF" && grep -q '^virtual_display=true$' "$CONF" && ok "profiles: Load into the Settings page saves its values" || fail "load did not save"
setconf width=2560 height=1440 fps=90 bitrate=80000
SECTION=mlxr setconf match_headset_fps=true virtual_display=false scale_factor=100
cp "$t/ini.profile" "$ini"
out="$(pops 'remove|Game~dump')"
expect "profiles: all gone" "$out" "remove|Game -> ok"
networkPath_now="$(grep '^networkPath=' "$CONF")"
[ "$networkPath_now" = "networkPath=auto" ] || setconf networkPath=auto

# --- 3e. lane wave4-launcher: the check before a stream (PLAN 4.8) and launch by app UUID. A fake PC on
# localhost answers serverinfo and applist from files (MLXR_LAUNCHER_PREFLIGHT_URL points the check at it).
pf="$t/fakepc"; mkdir -p "$pf"
cat >"$t/fakepc.py" <<'PY'
import http.server, os, sys, urllib.parse
root = sys.argv[1]
class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        name = urllib.parse.urlparse(self.path).path.strip('/')
        with open(os.path.join(root, 'requests.log'), 'a') as f:
            f.write(name + '\n')
        p = os.path.join(root, name + '.xml')
        if not name.isalnum() or not os.path.exists(p):
            self.send_response(404); self.end_headers(); return
        body = open(p, 'rb').read()
        self.send_response(200)
        self.send_header('Content-Type', 'application/xml')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)
    def log_message(self, *a):
        pass
s = http.server.HTTPServer(('127.0.0.1', 0), H)
open(os.path.join(root, 'port'), 'w').write(str(s.server_address[1]))
s.serve_forever()
PY
python3 -I "$t/fakepc.py" "$pf" >"$t/fakepc.log" 2>&1 & fakepc=$!
trap 'kill $fakepc 2>/dev/null; rm -rf "$t"' EXIT
for i in $(seq 50); do [ -s "$pf/port" ] && break; sleep 0.1; done
PFURL="http://127.0.0.1:$(cat "$pf/port")"
UUID_DESKTOP=8C3F5D2A-1B2C-4D3E-9F40-0A1B2C3D4E5F
UUID_ACO=0B1C2D3E-4F50-4617-8293-A4B5C6D7E8F9
srvinfo() { # perm, vd capable, vd ready, state, current game, pair status, codec support, extra xml
    {
        echo '<?xml version="1.0" encoding="utf-8"?>'
        echo '<root status_code="200"><hostname>gaming-pc</hostname><appversion>7.1.431.-1</appversion>'
        [ -n "$1" ] && echo "<Permission>$1</Permission>"
        [ -n "$2" ] && echo "<VirtualDisplayCapable>$2</VirtualDisplayCapable><VirtualDisplayDriverReady>$3</VirtualDisplayDriverReady>"
        echo "<state>$4</state><currentgame>$5</currentgame><PairStatus>$6</PairStatus><ServerCodecModeSupport>$7</ServerCodecModeSupport>${8:-}</root>"
    } > "$pf/serverinfo.xml"
}
cat >"$pf/applist.xml" <<APPS
<?xml version="1.0" encoding="utf-8"?>
<root status_code="200"><App><IsHdrSupported>0</IsHdrSupported><AppTitle>Desktop</AppTitle><UUID>$UUID_DESKTOP</UUID><ID>881448767</ID></App><App><AppTitle>Assassin's Creed Origins</AppTitle><UUID>$UUID_ACO</UUID><ID>1439608285</ID></App></root>
APPS
pfplan() { MLXR_LAUNCHER_PREFLIGHT=1 MLXR_LAUNCHER_PREFLIGHT_URL="${PFU:-$PFURL}" pplan "$1"; }
PERM_ALL=118693632   # 0x07131f00: launch, view, list, input, clipboard, server commands
srvinfo $PERM_ALL true true SUNSHINE_SERVER_FREE 0 1 769
: > "$pf/requests.log"
out="$(pfplan "gaming-pc:Desktop")"
echo "$out" | sed 's/^/    /'
expect "check: the PC is asked for serverinfo and the app list" "$(tr '\n' ' ' < "$pf/requests.log")" "serverinfo applist"
refute "check: Apollo with every permission and nothing set: no hint" "$out" "SELFTEST: hint:"
expect "launch by UUID: the app's UUID goes to the VR client" "$out" "SELFTEST: env: MLXR_APP_UUID=$UUID_DESKTOP"
expect "check: logged" "$(cat "$t/stderr")" "MLXR_LAUNCHER_PREFLIGHT: Apollo permission 118693632 virtual display ready"
out="$(pfplan "gaming-pc:Assassin's Creed Origins")"
expect "launch by UUID: by the app's id" "$out" "SELFTEST: env: MLXR_APP_UUID=$UUID_ACO"
# the virtual display on, its driver not ready
SECTION=mlxr setconf virtual_display=true
srvinfo $PERM_ALL true false SUNSHINE_SERVER_FREE 0 1 769
out="$(pfplan "gaming-pc:Desktop")"
expect "check: virtual display driver not ready" "$out" "hint: Use Virtual Display is on, but Apollo's virtual display driver is not ready on gaming-pc."
expect "check: the hint is logged" "$(cat "$t/stderr")" "MLXR_LAUNCHER_HINT: Use Virtual Display is on"
# not Apollo (no Permission, no virtual display), virtual display and clipboard sync on
SECTION=mlxr setconf clipboard_sync=true
srvinfo "" "" "" SUNSHINE_SERVER_FREE 0 1 769
out="$(pfplan "gaming-pc:Desktop")"
expect "check: a virtual display needs Apollo" "$out" "hint: Use Virtual Display is on, but gaming-pc does not look like Apollo."
expect "check: clipboard sync needs Apollo" "$out" "hint: Smart Clipboard Sync is on, but gaming-pc does not look like Apollo"
cp "$pf/applist.xml" "$t/applist.uuid"
sed -i "s/$UUID_DESKTOP//" "$pf/applist.xml"
out="$(pfplan "gaming-pc:Desktop")"
refute "launch by UUID: a host that sends none, no MLXR_APP_UUID" "$out" "MLXR_APP_UUID"
cp "$t/applist.uuid" "$pf/applist.xml"
SECTION=mlxr setconf virtual_display=false clipboard_sync=false
# Apollo's second client: list and view only; and one without mouse and keyboard
srvinfo 50331648 true true SUNSHINE_SERVER_FREE 0 1 769
out="$(pfplan "gaming-pc:Desktop")"
expect "check: no Launch Apps permission" "$out" "hint: gaming-pc does not let this client launch apps (Apollo permission Launch Apps)"
srvinfo $((0x07000100)) true true SUNSHINE_SERVER_FREE 0 1 769
out="$(pfplan "gaming-pc:Desktop")"
expect "check: input permissions missing" "$out" "hint: gaming-pc will ignore this client's mouse, keyboard input"
# over plain HTTP Apollo says Permission 0 and nothing of the virtual display: no false alarm
SECTION=mlxr setconf virtual_display=true
srvinfo 0 "" "" SUNSHINE_SERVER_FREE 0 0 769
out="$(pfplan "gaming-pc:Desktop")"
refute "check: Permission 0 from an unpaired answer is not believed" "$out" "SELFTEST: hint:"
SECTION=mlxr setconf virtual_display=false
# another app running
srvinfo $PERM_ALL true true SUNSHINE_SERVER_BUSY 1439608285 1 769
out="$(pfplan "gaming-pc:Desktop")"
expect "check: another app is running" "$out" "hint: gaming-pc is already running Assassin's Creed Origins, maybe for another client."
out="$(pfplan "gaming-pc:Assassin's Creed Origins")"
refute "check: the same app running is a resume, no hint" "$out" "SELFTEST: hint:"
# a resolution the PC does not list (GameStream's display modes), HEVC asked of a PC without it
srvinfo "" "" "" SUNSHINE_SERVER_FREE 0 1 1 '<SupportedDisplayMode><DisplayMode><Width>1920</Width><Height>1080</Height><RefreshRate>60</RefreshRate></DisplayMode></SupportedDisplayMode>'
setconf videocfg=2
out="$(pfplan "gaming-pc:Desktop")"
expect "check: a resolution the PC does not list" "$out" "hint: 2560x1440 is not one of the resolutions gaming-pc lists (1920x1080)."
expect "check: HEVC the PC does not have" "$out" "hint: Prefer HEVC is set, but gaming-pc does not advertise HEVC"
setconf videocfg=0
# a PC that does not answer: no hints, the stream goes on; the switch and the environment turn it off; a dry run asks nothing
out="$(PFU=http://127.0.0.1:9 pfplan "gaming-pc:Desktop")"
refute "check: a PC that does not answer gives no hint" "$out" "SELFTEST: hint:"
expect "check: not answered is logged" "$(cat "$t/stderr")" "MLXR_LAUNCHER_PREFLIGHT: not answered"
expect "check: the stream goes on" "$out" "SELFTEST: command: "
srvinfo 50331648 true true SUNSHINE_SERVER_FREE 0 1 769
SECTION=mlxr setconf preflight=false
: > "$pf/requests.log"
out="$(pfplan "gaming-pc:Desktop")"
[ ! -s "$pf/requests.log" ] && ok "check: off on the Settings page, the PC is not asked" || fail "asked with the switch off"
refute "check: off, no MLXR_APP_UUID" "$out" "MLXR_APP_UUID"
SECTION=mlxr setconf preflight=true
out="$(MLXR_LAUNCHER_PREFLIGHT=0 MLXR_LAUNCHER_PREFLIGHT_URL="$PFURL" pplan "gaming-pc:Desktop")"
[ ! -s "$pf/requests.log" ] && ok "check: MLXR_LAUNCHER_PREFLIGHT=0 turns it off" || fail "asked with MLXR_LAUNCHER_PREFLIGHT=0"
out="$(MLXR_LAUNCHER_PREFLIGHT=1 pplan "gaming-pc:Desktop")"
refute "check: a dry run does not ask the PC" "$(cat "$t/stderr")" "MLXR_LAUNCHER_PREFLIGHT"
# the hints reach the segue (a toast) and the error dialog when the stream then fails; the dry run of a stream
# carries MLXR_APP_UUID to the client
out="$(MLXR_LAUNCHER_PREFLIGHT=1 MLXR_LAUNCHER_PREFLIGHT_URL="$PFURL" MLXR_LAUNCHER_DRYRUN="fail:6:The host refused the launch" MLXR_NATIVE_CLIENT= selftest "gaming-pc:Desktop")"
expect "check: the hint is shown when the stream starts" "$out" "SELFTEST: warning: gaming-pc does not let this client launch apps"
expect "check: the hint is added to the error dialog" "$out" "error=The host refused the launch"
expect "check: under its own heading" "$out" "Found before the stream started:"
expect "launch by UUID: in the VR client's environment" "$out" "MLXR_LAUNCHER_DRYRUN_ENV: MLXR_APP_UUID=$UUID_DESKTOP"
srvinfo $PERM_ALL true true SUNSHINE_SERVER_FREE 0 1 769

# --- 3f. lane launcher-bundle (PLAN 6.7, 6.2, 6.4)
# 6.7: the VR client's config.ini keys the Settings page shows through MlxrPrefs.vr have the client's own kind,
# default, range and names (its parser's table, mlxr-settings-dump --table)
if [ -n "${MLXR_SETTINGS_DUMP:-}" ] && [ -x "$MLXR_SETTINGS_DUMP" ]; then
    "$MLXR_SETTINGS_DUMP" --table > "$t/client-table"
    MLXR_LAUNCHER_DRYRUN=1 selftest vrkeys > "$t/launcher-table"
    n="$(wc -l < "$t/launcher-table")"
    bad="$(grep -vxFf "$t/client-table" "$t/launcher-table")"
    [ "$n" -ge 40 ] && [ -z "$bad" ] && ok "the page's $n config.ini keys match the VR client's table" || fail "config.ini keys differ from the VR client's ($n keys): $bad"
    # Report only: a key of the client's that has no row on the page and is not left out on purpose (a new setting
    # wants a row, or a place on this list; launcher.md "Config-only VR keys")
    own="stream.resolution stream.fps stream.bitrate_kbps stream.codec stream.audio_config stream.frame_pacing stream.network_path
         stream.doff_disconnect stream.idle_disconnect stream.bitrate_wifi stream.bitrate_adapter stream.bitrate_wired stream.bitrate_usb
         stream.rfi_idr_ms stream.failover stream.failover_burst_ms vr.keyboard_keymap vr.controller_model vr.flip_face_buttons
         vr.pad_mouse_chord vr.pad_mouse_scroll vr.pad_arbitration vr.pad_gyro vr.notices vr.lightgun vr.scroll_speed
         vr.background_dim vr.void_color vr.first_run_shown vr.pad_card display.zoom display.stats display.glow_feather
         3d.separation 3d.convergence 3d.stereo 3d.eye_balance 3d.stereo_source 3d.sbs_full 3d.edge_sharpness 3d.depth_scale
         3d.depth_global 3d.motion_reset ui.language vr.head_dwell_ms"
    left="stream.host stream.app vr.hand_hint_seen display.cog_tab display.zoom_x display.zoom_y screen.height_cm
          screen.azimuth_deg screen.tilt_deg screen.rotate_deg screen.back_distance_cm screen.back_height_cm
          screen.back_azimuth_deg screen.back_tilt_deg screen.back_rotate_deg screen.back_curve screen.back_width_cm"
    for k in $(cut -d' ' -f1 "$t/client-table"); do
        case "$k" in debug.*|rooms.*) continue;; esac
        case " $(echo $own $left) " in *" $k "*) continue;; esac
        grep -q "^$k " "$t/launcher-table" || echo "note: config.ini key $k has no row on the Settings page"
    done
fi

# 6.2: the update check. upd [VAR=value ...] -> its log lines; GitHub's answer is a file or a fake on 127.0.0.1
upd() { env MLXR_LAUNCHER_DRYRUN=1 MLXR_LAUNCHER_UPDATE=1 "$@" MLXR_LAUNCHER_SELFTEST=update "$L" 2>&1 | grep -E 'MLXR_LAUNCHER_UPDATE|SELFTEST: update'; }
VER="$(head -n 1 "$here/../../VERSION")"
REL=https://github.com/simplysanatori/moonlight-xr-steam-frame/releases
rel() { printf '{"tag_name":"%s","html_url":"%s","draft":false,"prerelease":%s,"assets":[]}' "$1" "${2:-$REL/tag/$1}" "${3:-false}" > "$t/rel.json"; }
rel v999.0.0
o="$(upd MLXR_LAUNCHER_UPDATE_JSON="$t/rel.json")"
expect "update: a newer release is offered with its page" "$o" "SELFTEST: update available 999.0.0 $REL/tag/v999.0.0"
expect "update: and logged with the installed version" "$o" "version 999.0.0 is out (installed $VER)"
rel "v$VER"
o="$(upd MLXR_LAUNCHER_UPDATE_JSON="$t/rel.json")"
expect "update: the installed version is up to date" "$o" "up to date (latest release $VER, installed $VER)"
refute "update: no notice for the same version" "$o" "available"
rel v0.0.1
refute "update: no notice for an older release" "$(upd MLXR_LAUNCHER_UPDATE_JSON="$t/rel.json")" "available"
rel v999.0.0 "" true
expect "update: a pre-release is not offered" "$(upd MLXR_LAUNCHER_UPDATE_JSON="$t/rel.json")" "check failed: the latest release is a draft or a pre-release"
rel v999.0.0 "https://example.invalid/not-this-project"
expect "update: a page outside this project's releases is not opened" "$(upd MLXR_LAUNCHER_UPDATE_JSON="$t/rel.json")" "available 999.0.0 $REL/latest"
echo '<html>rate limited</html>' > "$t/bad.json"
o="$(upd MLXR_LAUNCHER_UPDATE_JSON="$t/bad.json")"
expect "update: an answer that is not JSON is one log line" "$o" "check failed: the answer is not a JSON object"
[ "$(echo "$o" | grep -c MLXR_LAUNCHER_UPDATE)" = 1 ] && ok "update: a failure is exactly one log line" || fail "update failure lines: $o"
rel v999.0.0
SECTION=mlxr setconf check_updates=false
o="$(upd MLXR_LAUNCHER_UPDATE_JSON="$t/rel.json")"
expect "update: Check for updates off asks nothing" "$o" "off (Settings, Check for updates)"
refute "update: and offers nothing" "$o" "available"
SECTION=mlxr setconf check_updates=true
refute "update: stock mode (MLXR_HANDOFF=0) never checks" "$(upd MLXR_HANDOFF=0 MLXR_LAUNCHER_UPDATE_JSON="$t/rel.json")" "MLXR_LAUNCHER_UPDATE"
# a fake GitHub on 127.0.0.1: what is asked, how often, and failures
cat > "$t/fakegh.py" <<'PY'
import http.server, os, sys
d = sys.argv[1]
class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        with open(os.path.join(d, "gh.log"), "a") as f:
            f.write("%s UA=%s ACCEPT=%s\n" % (self.path, self.headers.get("User-Agent"), self.headers.get("Accept")))
        code = int(open(os.path.join(d, "gh.status")).read().strip())
        body = open(os.path.join(d, "rel.json"), "rb").read()
        self.send_response(code); self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body))); self.end_headers(); self.wfile.write(body)
    def log_message(self, *a): pass
s = http.server.HTTPServer(("127.0.0.1", 0), H)
print(s.server_address[1], flush=True)
s.serve_forever()
PY
echo 200 > "$t/gh.status"; : > "$t/gh.log"
python3 -I "$t/fakegh.py" "$t" > "$t/gh.port" 2>/dev/null &
GHPID=$!
for _ in $(seq 50); do [ -s "$t/gh.port" ] && break; sleep 0.1; done
GHURL="http://127.0.0.1:$(cat "$t/gh.port")/repos/simplysanatori/moonlight-xr-steam-frame/releases/latest"
SECTION=mlxr setconf update_checked=
o="$(upd MLXR_LAUNCHER_UPDATE_URL="$GHURL")"
expect "update: asks the releases API once" "$(cat "$t/gh.log")" "/repos/simplysanatori/moonlight-xr-steam-frame/releases/latest UA=moonlight-xr-launcher ACCEPT=application/vnd.github+json"
expect "update: the answer offers the release" "$o" "available 999.0.0"
o="$(upd MLXR_LAUNCHER_UPDATE_URL="$GHURL")"
[ "$(wc -l < "$t/gh.log")" = 1 ] && ok "update: at most once a day (the second start asks nothing)" || fail "update asked again: $(cat "$t/gh.log")"
expect "update: the second start still shows the kept answer" "$o" "available 999.0.0"
expect "update: and says when it asks again" "$o" "next check after"
SECTION=mlxr setconf update_checked="$(date -u -d '-2 days' +%Y-%m-%dT%H:%M:%SZ)"
upd MLXR_LAUNCHER_UPDATE_URL="$GHURL" >/dev/null
[ "$(wc -l < "$t/gh.log")" = 2 ] && ok "update: a day later it asks again" || fail "update after two days: $(cat "$t/gh.log")"
grep -q "^update_checked=$(date -u +%Y-%m-%d)" "$CONF" && ok "update: the check's time is kept" || fail "update_checked: $(grep update_ "$CONF")"
echo 500 > "$t/gh.status"; SECTION=mlxr setconf update_checked=
o="$(upd MLXR_LAUNCHER_UPDATE_URL="$GHURL")"
expect "update: an HTTP error is one log line" "$o" "check failed: HTTP 500"
[ "$(echo "$o" | grep -c MLXR_LAUNCHER_UPDATE)" = 1 ] && ok "update: and nothing else" || fail "update HTTP 500 lines: $o"
kill "$GHPID" 2>/dev/null; wait "$GHPID" 2>/dev/null
SECTION=mlxr setconf update_checked=
o="$(upd MLXR_LAUNCHER_UPDATE_URL="$GHURL")"
[ "$(echo "$o" | grep -c 'MLXR_LAUNCHER_UPDATE: check failed')" = 1 ] && ok "update: no answer at all is one log line" || fail "update without a server: $o"
SECTION=mlxr setconf update_checked= update_latest= update_url=

# 6.4: On start. onstart [running app id] -> what the launcher would start with the PC online
onstart() { MLXR_LAUNCHER_DRYRUN=1 selftest "onstart${1:+:$1}"; }
expect "On start: off by default" "$(onstart)" "mode=none pc= app= nothing: off"
SECTION=mlxr setconf on_start=desktop last_pc=
expect "On start: Last PC's Desktop with no stream yet starts nothing" "$(onstart)" "nothing: no stream has gone to a PC yet"
dry "gaming-pc:Desktop" >/dev/null
grep -q '^last_pc=gaming-pc$' "$CONF" && ok "On start: a stream keeps its PC as the last PC" || fail "last_pc: $(grep last_pc "$CONF")"
o="$(onstart)"
expect "On start: the last PC's Desktop" "$o" "mode=desktop pc=gaming-pc app=Desktop"
expect "On start: starts once the PC is online" "$o" "verdict=start 881448767 Desktop"
expect "On start: another app running on the PC is left alone" "$(onstart 1439608285)" "verdict=busy Assassin's Creed Origins"
expect "On start: the same app running is resumed" "$(onstart 881448767)" "verdict=start 881448767 Desktop"
SECTION=mlxr setconf on_start=app on_start_pc=GAMING-PC "on_start_app=assassin's creed origins"
expect "On start: a chosen app, names matched without case" "$(onstart)" "verdict=start 1439608285 Assassin's Creed Origins"
SECTION=mlxr setconf on_start_app=Nothing
expect "On start: an app the PC does not list (yet) is waited for" "$(onstart)" "verdict=wait"
SECTION=mlxr setconf on_start=none on_start_pc= on_start_app=

# --- 4. a client that runs: control comes back with its result
fake="$t/fake-client.sh"
cat >"$fake" <<'FAKE'
#!/bin/bash
echo "args: $*" >> "$FAKE_LOG"
echo "cwd: $(pwd)" >> "$FAKE_LOG"
echo "ld_library_path: [${LD_LIBRARY_PATH-unset}]" >> "$FAKE_LOG"
[ "${FAKE_MODE:-}" = stream ] && echo MLXR_STREAMING
sleep "${FAKE_SLEEP:-0.5}"
case "${FAKE_MODE:-ok}" in
  ok|stream) exit 0;;
  err) echo "some log line"; echo "MLXR_ERROR: The host is on fire" >&2; exit 6;;
  silent) exit 3;;
  disc) exit 11;;
  crash) kill -SEGV $$;;
esac
FAKE
chmod +x "$fake"
export FAKE_LOG="$t/fake.log"
LD_LIBRARY_PATH=/steam/runtime MLXR_NATIVE_CLIENT="$fake" selftest "gaming-pc:Desktop" >"$t/stdout"; rc=$?
cat "$t/stdout"
[ $rc = 0 ] && ok "client exit 0 -> success" || fail "client exit 0 gave $rc"
expect "started signal and finish" "$(cat "$t/stdout")" "started=yes finished=yes error=none"
expect "client got the command line" "$(cat "$FAKE_LOG")" "--host $HOSTID --app 881448767 --mode 2560x1440x90 --bitrate 80000 --fps match --network auto --exit-to-caller"
expect "client ran in its own directory" "$(cat "$FAKE_LOG")" "cwd: $t"
expect "LD_LIBRARY_PATH not passed on" "$(cat "$FAKE_LOG")" "ld_library_path: [unset]"
grep -q "launcher runs" "$MLXR_LOG_DIR/moonlight-console.log" && ok "client output goes to moonlight-console.log" || fail "console log"
FAKE_MODE=err MLXR_NATIVE_CLIENT="$fake" selftest "gaming-pc:Desktop" >"$t/stdout"; rc=$?
[ $rc = 1 ] && ok "client failure -> error (exit 1)" || fail "exit code for failure was $rc"
expect "client error message reaches the dialog" "$(cat "$t/stdout")" "error=The host is on fire"
FAKE_MODE=silent MLXR_NATIVE_CLIENT="$fake" selftest "gaming-pc:Desktop" >"$t/stdout"; rc=$?
expect "code without a message" "$(cat "$t/stdout")" "stopped with code 3"
FAKE_MODE=disc MLXR_NATIVE_CLIENT="$fake" selftest "gaming-pc:Desktop" >"$t/stdout"; rc=$?
[ $rc = 0 ] && expect "disconnect chord (exit 11) is not an error" "$(cat "$t/stdout")" "error=none" || fail "exit 11 gave $rc"
FAKE_MODE=crash MLXR_NATIVE_CLIENT="$fake" selftest "gaming-pc:Desktop" >"$t/stdout"; rc=$?
expect "crash reported" "$(cat "$t/stdout")" "crashed"
MLXR_LAUNCHER_DRYRUN="fail:5:no such app" selftest "gaming-pc:Desktop" >"$t/stdout"; expect "dry-run failure injection" "$(cat "$t/stdout")" "error=no such app"
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
    # defined here, before the first section that checks a page's output with it (lane a11y-2: the Accessibility
    # section's check ran before this was defined further down, and was skipped)
    uiout() { grep -E "$QMLERR" "$1" && fail "$2: QML errors" || ok "$2: no QML errors"; }
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
    MLXR_LAUNCHER_IW_FILE=/dev/null MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_PAGE=settings MLXR_LAUNCHER_CLICK=statsRow,flipRow,noticesRow,scrollSpeedRow,doffRow,idleRow,controllerModelRow,hdrRow,stereoSourceRow,sbsFullRow,idrCapRow,padCardRow,firstRunRow,pathFailoverRow,failoverBurstRow \
        MLXR_LAUNCHER_SCREENSHOT="$SHOTS/settings-clicked.png@3000" "$L" >"$t/ui6.out" 2>&1
    grep -q "no clickable" "$t/ui6.out" && fail "click hook: $(grep 'no clickable' "$t/ui6.out")"
    grep -q '^stats = true$' "$ini" && ok "stats overlay row writes [display] stats = true" || { fail "stats not written"; cat "$ini"; }
    grep -q '^#stats' "$ini" && fail "commented stats line left as well" || ok "the commented default line was replaced"
    grep -A1 '^\[vr\]' "$ini" | grep -q '^flip_face_buttons = true$' && ok "flip row adds [vr] flip_face_buttons = true" || { fail "flip not written"; cat "$ini"; }
    grep -A2 '^\[vr\]' "$ini" | grep -q '^notices = important$' && ok "notices row steps All to Important and writes [vr] notices" || { fail "notices not written"; cat "$ini"; }
    grep -A6 '^\[vr\]' "$ini" | grep -q '^scroll_speed = 150$' && ok "scroll speed row steps 100 to 150 and writes [vr] scroll_speed" || { fail "scroll_speed not written"; cat "$ini"; }
    grep -A6 '^\[vr\]' "$ini" | grep -q '^controller_model = frame$' && ok "controller row steps Off to Steam Frame and writes [vr] controller_model" || { fail "controller_model not written"; cat "$ini"; }
    grep -A3 '^\[stream\]' "$ini" | grep -q '^doff_disconnect = 120$' && ok "headset-off row steps 60 s to 120 s and writes [stream] doff_disconnect" || { fail "doff_disconnect not written"; cat "$ini"; }
    grep -A3 '^\[stream\]' "$ini" | grep -q '^idle_disconnect = 10$' && ok "idle row steps Never to 10 minutes and writes [stream] idle_disconnect" || { fail "idle_disconnect not written"; cat "$ini"; }
    # lane wave4-sbs (PLAN 4.3): the 3D source row steps Depth to SBS, then the full-size switch comes alive
    grep -A6 '^\[3d\]' "$ini" | grep -q '^stereo_source = sbs$' && ok "3D source row steps Depth to SBS and writes [3d] stereo_source" || { fail "stereo_source not written"; cat "$ini"; }
    grep -A6 '^\[3d\]' "$ini" | grep -q '^sbs_full = true$' && ok "full-size SBS switch writes [3d] sbs_full = true" || { fail "sbs_full not written"; cat "$ini"; }
    # lane wave1-burst: the full-frame request cap steps 500 ms to 1000 ms
    grep -A4 '^\[stream\]' "$ini" | grep -q '^rfi_idr_ms = 1000$' && ok "full-frame request row steps 500 ms to 1000 ms and writes [stream] rfi_idr_ms" || { fail "rfi_idr_ms not written"; cat "$ini"; }
    # lane wave3-cards: the gamepad card's row steps Auto to On, the controls card's switch (on: not shown yet) turns off
    sed -n '/^\[vr\]/,/^\[/p' "$ini" | grep -q '^pad_card = on$' && ok "gamepad card row steps Auto to On and writes [vr] pad_card" || { fail "pad_card not written"; cat "$ini"; }
    sed -n '/^\[vr\]/,/^\[/p' "$ini" | grep -q '^first_run_shown = true$' && ok "controls card switch off writes [vr] first_run_shown = true" || { fail "first_run_shown not written"; cat "$ini"; }
    # lane wave1-failover: the path failover switch turns on, its loss length steps 3 s to 5 s
    grep -A8 '^\[stream\]' "$ini" | grep -q '^failover = on$' && ok "path failover switch writes [stream] failover = on" || { fail "failover not written"; cat "$ini"; }
    grep -A8 '^\[stream\]' "$ini" | grep -q '^failover_burst_ms = 5000$' && ok "loss length row steps 3 s to 5 s and writes [stream] failover_burst_ms" || { fail "failover_burst_ms not written"; cat "$ini"; }
    grep -q '^glow = true$' "$ini" && grep -q '^separation = 9$' "$ini" && grep -q '^stereo = false$' "$ini" && ok "other config.ini keys untouched" || { fail "config.ini keys changed"; diff "$t/ini.before" "$ini"; }
    grep -q -i hdr "$ini" && fail "greyed HDR row changed something" || ok "greyed HDR row is inert"
    grep -q "MLXR_LAUNCHER_WIFI: Wi-Fi$" "$t/ui6.out" && ok "Network connection: plain Wi-Fi when unknown" || fail "wifi fallback: $(grep MLXR_LAUNCHER_WIFI "$t/ui6.out")"
    # lane wave4c-usb: the USB cable row. A fake /sys/class/net: usb0 with carrier, a wired port without
    mkdir -p "$t/netroot/usb0" "$t/netroot/eth0"
    echo 1 > "$t/netroot/usb0/carrier"; echo 0 > "$t/netroot/eth0/carrier"
    MLXR_LAUNCHER_NET_ROOT="$t/netroot" MLXR_LAUNCHER_IW_FILE=/dev/null MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_PAGE=settings \
        MLXR_LAUNCHER_SCREENSHOT="$t/usb-cable.png@3000" "$L" >"$t/ui7.out" 2>&1
    grep -Eq "$QMLERR" "$t/ui7.out" && { fail "usb row QML errors"; grep -E "$QMLERR" "$t/ui7.out" | head; } || ok "settings page loads with the USB cable row (no QML errors)"
    grep -q "MLXR_LAUNCHER_USB: cable on usb0" "$t/ui7.out" && ok "USB cable: usb0 with carrier is detected" || fail "usb cable not detected: $(grep MLXR_LAUNCHER_USB "$t/ui7.out")"
    echo 0 > "$t/netroot/usb0/carrier"
    MLXR_LAUNCHER_NET_ROOT="$t/netroot" MLXR_LAUNCHER_IW_FILE=/dev/null MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_PAGE=settings \
        MLXR_LAUNCHER_SCREENSHOT="$t/usb-nocable.png@3000" "$L" >"$t/ui8.out" 2>&1
    grep -q "MLXR_LAUNCHER_USB: no cable" "$t/ui8.out" && ok "USB cable: usb0 without carrier is ignored" || fail "usb0 without carrier: $(grep MLXR_LAUNCHER_USB "$t/ui8.out")"
    # lane wave4b-abr: one stepped bitrate row per network path under Network connection ("Same as above" at 0)
    rep() { local n="$1" c="$2" o="" i; for ((i = 0; i < c; i++)); do o="$o${o:+,}$n"; done; printf '%s' "$o"; }
    abr_clicks="$(rep bitrateWifiRow 7),$(rep bitrateAdapterRow 10),$(rep bitrateUsbRow 14),$(rep bitrateWiredRow 15)"
    MLXR_LAUNCHER_IW_FILE=/dev/null MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_PAGE=settings MLXR_LAUNCHER_CLICK="$abr_clicks" \
        MLXR_LAUNCHER_SCREENSHOT="$SHOTS/settings-path-bitrates.png@3000" "$L" >"$t/uiabr1.out" 2>&1
    grep -q "no clickable" "$t/uiabr1.out" && fail "bitrate rows not found: $(grep 'no clickable' "$t/uiabr1.out" | head -2)"
    grep -Eq "$QMLERR" "$t/uiabr1.out" && { fail "path bitrate rows: QML errors"; grep -E "$QMLERR" "$t/uiabr1.out" | head; } || ok "settings page loads with the path bitrate rows (no QML errors)"
    grep -A12 '^\[stream\]' "$ini" | grep -q '^bitrate_wifi = 80000$' && ok "Wi-Fi bitrate row steps 0 up to 80 Mbps and writes [stream] bitrate_wifi" || { fail "bitrate_wifi not written"; cat "$ini"; }
    grep -A12 '^\[stream\]' "$ini" | grep -q '^bitrate_adapter = 150000$' && ok "adapter bitrate row steps to 150 Mbps and writes [stream] bitrate_adapter" || { fail "bitrate_adapter not written"; cat "$ini"; }
    grep -A12 '^\[stream\]' "$ini" | grep -q '^bitrate_usb = 500000$' && ok "USB bitrate row steps to the 500 Mbps cap and writes [stream] bitrate_usb" || { fail "bitrate_usb not written"; cat "$ini"; }
    grep -q '^bitrate_wired' "$ini" && fail "wired bitrate row wrapped back to Same as above but wrote a key" || ok "wired bitrate row wraps from 500 Mbps back to Same as above and writes nothing"
    grep -q '^doff_disconnect = 120$' "$ini" && grep -q '^idle_disconnect = 10$' "$ini" && ok "other [stream] keys untouched by the bitrate rows" || { fail "[stream] keys changed"; cat "$ini"; }
    # a second page start reads the keys back: the 80000 steps on to 100000, and the value shows in the row
    MLXR_LAUNCHER_IW_FILE=/dev/null MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_PAGE=settings MLXR_LAUNCHER_CLICK=bitrateWifiRow \
        MLXR_LAUNCHER_SCREENSHOT="$t/path-bitrates-2.png@3000" "$L" >"$t/uiabr2.out" 2>&1
    grep -q '^bitrate_wifi = 100000$' "$ini" && ok "path bitrates are read back from config.ini (80000 steps to 100000)" || { fail "bitrate_wifi not read back"; cat "$ini"; }
    # PC VR was taken out: the keys an older version saved for it load without trouble and are dropped
    SECTION=mlxr setconf pcvr_client=76561198000000001 pcvr_client_name=gaming-pc pcvr_app=620980 "pcvr_app_name=Beat Saber"
    MLXR_LAUNCHER_IW_FILE=/dev/null MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_PAGE=settings \
        MLXR_LAUNCHER_SCREENSHOT="$t/old-pcvr.png@3000" "$L" >"$t/uipcvr.out" 2>&1
    grep -Eq "$QMLERR" "$t/uipcvr.out" && { fail "old PC VR keys: QML errors"; grep -E "$QMLERR" "$t/uipcvr.out" | head -3; } || ok "settings page loads with an old config's PC VR keys (no QML errors)"
    grep -q '^pcvr_' "$CONF" && fail "old PC VR keys kept after a save: $(grep '^pcvr_' "$CONF" | tr '\n' ' ')" || ok "old PC VR keys are dropped"
    # what the VR client's own parser makes of it
    if [ -n "${MLXR_SETTINGS_DUMP:-}" ] && [ -x "$MLXR_SETTINGS_DUMP" ]; then
        dump="$("$MLXR_SETTINGS_DUMP" "$ini")"
        expect "VR client reads stats" "$dump" "display.stats = true"
        expect "VR client reads flip" "$dump" "vr.flip_face_buttons = true"
        expect "VR client reads notices" "$dump" "vr.notices = important"
        expect "VR client reads scroll speed" "$dump" "vr.scroll_speed = 150"
        expect "VR client reads controller_model" "$dump" "vr.controller_model = frame"
        expect "VR client reads doff_disconnect" "$dump" "stream.doff_disconnect = 120"
        expect "VR client reads idle_disconnect" "$dump" "stream.idle_disconnect = 10"
        expect "VR client reads bitrate_wifi" "$dump" "stream.bitrate_wifi = 100000"
        expect "VR client reads bitrate_adapter" "$dump" "stream.bitrate_adapter = 150000"
        expect "VR client reads bitrate_usb" "$dump" "stream.bitrate_usb = 500000"
        expect "VR client reads stereo_source" "$dump" "3d.stereo_source = sbs"
        expect "VR client reads sbs_full" "$dump" "3d.sbs_full = true"
        expect "VR client reads rfi_idr_ms" "$dump" "stream.rfi_idr_ms = 1000"
        expect "VR client reads pad_card" "$dump" "vr.pad_card = on"
        expect "VR client reads first_run_shown" "$dump" "vr.first_run_shown = true"
        expect "VR client reads failover" "$dump" "stream.failover = on"
        expect "VR client reads failover_burst_ms" "$dump" "stream.failover_burst_ms = 5000"
    fi
    # lane fix-polish: Show controllers has no Generic choice. A file's old "generic" (and its old number 2) reads
    # as the Steam Frame controller, here and in the VR client, so one press steps it on to Gun
    printf '[vr]\ncontroller_model = generic\n' > "$ini"
    MLXR_LAUNCHER_IW_FILE=/dev/null MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_PAGE=settings MLXR_LAUNCHER_CLICK=controllerModelRow \
        MLXR_LAUNCHER_SCREENSHOT="$t/model-generic.png@3000" "$L" >"$t/uimodel.out" 2>&1
    grep -q "no clickable" "$t/uimodel.out" && fail "controller row click hook: $(grep 'no clickable' "$t/uimodel.out")"
    grep -A2 '^\[vr\]' "$ini" | grep -q '^controller_model = gun$' && ok "an old generic reads as Steam Frame: the row steps it to Gun" || { fail "generic did not step to gun"; cat "$ini"; }
    if [ -n "${MLXR_SETTINGS_DUMP:-}" ] && [ -x "$MLXR_SETTINGS_DUMP" ]; then
        for old in generic 2; do
            printf '[vr]\ncontroller_model = %s\n' "$old" > "$t/model-old.ini"
            expect "VR client reads controller_model = $old as frame" "$("$MLXR_SETTINGS_DUMP" "$t/model-old.ini")" "vr.controller_model = frame"
        done
        printf '[vr]\ncontroller_model = 3\n' > "$t/model-old.ini"
        expect "VR client reads the old number of gun" "$("$MLXR_SETTINGS_DUMP" "$t/model-old.ini")" "vr.controller_model = gun"
    fi
    # lane wave3-visual: the surroundings rows (Picture tab's Background and Glow edge, 3D eye balance) step
    # from the file's values and write only their keys; the void colour is read as typed and kept
    printf '[vr]\nvoid_color = #204060\n\n[3d]\nseparation = 9\n' > "$ini"
    MLXR_LAUNCHER_IW_FILE=/dev/null MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_PAGE=settings MLXR_LAUNCHER_CLICK=backgroundDimRow,backgroundDimRow,glowFeatherRow,eyeBalanceRow,eyeBalanceRow \
        MLXR_LAUNCHER_SCREENSHOT="$SHOTS/settings-surroundings.png@3000" "$L" >"$t/ui6b.out" 2>&1
    grep -q "no clickable" "$t/ui6b.out" && fail "surroundings click hook: $(grep 'no clickable' "$t/ui6b.out")"
    grep -Eq "$QMLERR" "$t/ui6b.out" && { fail "surroundings QML errors"; grep -E "$QMLERR" "$t/ui6b.out" | head; } || ok "surroundings rows load (no QML errors)"
    grep -A4 '^\[vr\]' "$ini" | grep -q '^background_dim = 50$' && ok "background row steps 0 to 25 to 50 and writes [vr] background_dim" || { fail "background_dim not written"; cat "$ini"; }
    grep -A2 '^\[display\]' "$ini" | grep -q '^glow_feather = soft$' && ok "glow edge row steps Off to Soft and writes [display] glow_feather" || { fail "glow_feather not written"; cat "$ini"; }
    grep -A4 '^\[3d\]' "$ini" | grep -q '^eye_balance = 100$' && ok "eye balance row steps 0 to 50 to 100 and writes [3d] eye_balance" || { fail "eye_balance not written"; cat "$ini"; }
    grep -q '^void_color = #204060$' "$ini" && grep -q '^separation = 9$' "$ini" && ok "void colour and the other keys untouched" || { fail "void_color or separation changed"; cat "$ini"; }
    if [ -n "${MLXR_SETTINGS_DUMP:-}" ] && [ -x "$MLXR_SETTINGS_DUMP" ]; then
        dump="$("$MLXR_SETTINGS_DUMP" "$ini")"
        expect "VR client reads background_dim" "$dump" "vr.background_dim = 50"
        expect "VR client reads void_color" "$dump" "vr.void_color = #204060"
        expect "VR client reads glow_feather" "$dump" "display.glow_feather = soft"
        expect "VR client reads eye_balance" "$dump" "3d.eye_balance = 100"
    fi
    # lane wave6-i18n: the Headset language row shows the languages; picking one writes [ui] language,
    # which the VR client reads (at its next start); the rest of the file is kept
    cp "$ini" "$t/ini.lang-before"
    MLXR_LAUNCHER_IW_FILE=/dev/null MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_PAGE=settings MLXR_LAUNCHER_CLICK=headsetLanguageRow,headsetLang_de \
        MLXR_LAUNCHER_SCREENSHOT="$t/language.png@3000" "$L" >"$t/lang.out" 2>&1
    grep -q "no clickable" "$t/lang.out" && fail "language click hook: $(grep 'no clickable' "$t/lang.out")"
    grep -Eq "$QMLERR" "$t/lang.out" && { fail "language row QML errors"; grep -E "$QMLERR" "$t/lang.out" | head; } || ok "headset language row loads (no QML errors)"
    grep -A1 '^\[ui\]' "$ini" | grep -q '^language = de$' && ok "headset language row writes [ui] language = de" || { fail "[ui] language not written"; cat "$ini"; }
    diff <(grep -v '^\[ui\]$\|^language = \|^$' "$t/ini.lang-before") <(grep -v '^\[ui\]$\|^language = \|^$' "$ini") >/dev/null && ok "language row leaves the other keys alone" || { fail "language row changed other keys"; diff "$t/ini.lang-before" "$ini"; }
    if [ -n "${MLXR_SETTINGS_DUMP:-}" ] && [ -x "$MLXR_SETTINGS_DUMP" ]; then
        expect "VR client reads the headset language" "$("$MLXR_SETTINGS_DUMP" "$ini")" "ui.language = de"
    fi
    # the locale's spelling in the file reads as the client spells it, and survives a save untouched
    sed -i 's/^language = de$/language = pt_br/' "$ini"
    MLXR_LAUNCHER_IW_FILE=/dev/null MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_PAGE=settings MLXR_LAUNCHER_CLICK=headsetLanguageRow \
        MLXR_LAUNCHER_SCREENSHOT="$t/language2.png@3000" "$L" >"$t/lang2.out" 2>&1
    grep -q '^language = pt_br$' "$ini" && ok "an unchanged headset language is not rewritten" || { fail "[ui] language rewritten"; grep -A1 '^\[ui\]' "$ini"; }
    # lane wave5-depth: the 3D rows that only the file and this page carry (3D on, so they are live)
    printf '[3d]\nstereo = true\nseparation = 9\n' > "$ini"
    MLXR_LAUNCHER_IW_FILE=/dev/null MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_PAGE=settings MLXR_LAUNCHER_CLICK=edgeRow,motionRow,depthScaleRow,depthGlobalRow,zoomRow \
        MLXR_LAUNCHER_SCREENSHOT="$t/settings-3d.png@3000" "$L" >"$t/ui9.out" 2>&1
    grep -q "no clickable" "$t/ui9.out" && fail "3d click hook: $(grep 'no clickable' "$t/ui9.out")"
    grep -A4 '^\[3d\]' "$ini" | grep -q '^edge_sharpness = 25$' && ok "edge row steps 0 to 25 and writes [3d] edge_sharpness" || { fail "edge_sharpness not written"; cat "$ini"; }
    grep -A4 '^\[3d\]' "$ini" | grep -q '^motion_reset = 5$' && ok "motion row steps off to 5 and writes [3d] motion_reset" || { fail "motion_reset not written"; cat "$ini"; }
    grep -A6 '^\[3d\]' "$ini" | grep -q '^depth_scale = 125$' && ok "depth detail row steps 100 to 125 and writes [3d] depth_scale" || { fail "depth_scale not written"; cat "$ini"; }
    grep -A6 '^\[3d\]' "$ini" | grep -q '^depth_global = 125$' && ok "depth shape row steps 100 to 125 and writes [3d] depth_global" || { fail "depth_global not written"; cat "$ini"; }
    # lane wave4b-zoom (PLAN 4b.6): the zoom row steps 100 to 125 and writes [display] zoom
    grep -A2 '^\[display\]' "$ini" | grep -q '^zoom = 125$' && ok "zoom row steps 100 to 125 and writes [display] zoom" || { fail "zoom not written"; cat "$ini"; }
    grep -q '^separation = 9$' "$ini" && grep -q '^stereo = true$' "$ini" && ok "3d rows leave the other [3d] keys alone" || { fail "[3d] keys changed"; cat "$ini"; }
    if [ -n "${MLXR_SETTINGS_DUMP:-}" ] && [ -x "$MLXR_SETTINGS_DUMP" ]; then
        dump="$("$MLXR_SETTINGS_DUMP" "$ini")"
        expect "VR client reads edge_sharpness" "$dump" "3d.edge_sharpness = 25"
        expect "VR client reads motion_reset" "$dump" "3d.motion_reset = 5"
        expect "VR client reads depth_scale" "$dump" "3d.depth_scale = 125"
        expect "VR client reads depth_global" "$dump" "3d.depth_global = 125"
        expect "VR client reads zoom" "$dump" "display.zoom = 125"
    fi
    # lane wave4-headinput: the head and pointer input rows under Input Settings write only their keys
    printf '[vr]\nhead_aim = true\nopentrack_host = gaming-pc.local\nopentrack_port = 5555\n' > "$ini"
    MLXR_LAUNCHER_IW_FILE=/dev/null MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_PAGE=settings \
        MLXR_LAUNCHER_CLICK=headLookRow,headLookPxRow,opentrackRow,pointerSourceRow \
        MLXR_LAUNCHER_SCREENSHOT="$t/settings-headinput.png@3000" "$L" >"$t/ui13.out" 2>&1
    grep -q "no clickable" "$t/ui13.out" && fail "head input click hook: $(grep 'no clickable' "$t/ui13.out")"
    grep -Eq "$QMLERR" "$t/ui13.out" && { fail "head input rows: QML errors"; grep -E "$QMLERR" "$t/ui13.out" | head -3; } || ok "head input rows load (no QML errors)"
    grep -A8 '^\[vr\]' "$ini" | grep -q '^head_look = true$' && ok "head-look switch writes [vr] head_look = true" || { fail "head_look not written"; cat "$ini"; }
    grep -A8 '^\[vr\]' "$ini" | grep -q '^head_look_px_deg = 12$' && ok "head-look speed row steps 8 to 12 and writes [vr] head_look_px_deg" || { fail "head_look_px_deg not written"; cat "$ini"; }
    grep -q '^head_aim = true$' "$ini" && ok "head input rows leave head_aim alone" || { fail "head_aim changed"; cat "$ini"; }
    grep -A8 '^\[vr\]' "$ini" | grep -q '^opentrack = true$' && ok "opentrack switch writes [vr] opentrack = true" || { fail "opentrack not written"; cat "$ini"; }
    grep -q '^opentrack_host = gaming-pc.local$' "$ini" && grep -q '^opentrack_port = 5555$' "$ini" && ok "opentrack host and port read and kept" || { fail "opentrack host or port changed"; cat "$ini"; }
    if [ -n "${MLXR_SETTINGS_DUMP:-}" ] && [ -x "$MLXR_SETTINGS_DUMP" ]; then
        dump="$("$MLXR_SETTINGS_DUMP" "$ini")"
        expect "VR client reads head_look" "$dump" "vr.head_look = true"
        expect "VR client reads head_look_px_deg" "$dump" "vr.head_look_px_deg = 12"
        expect "VR client reads opentrack" "$dump" "vr.opentrack = true"
        expect "VR client reads opentrack_host" "$dump" "vr.opentrack_host = gaming-pc.local"
        expect "VR client reads opentrack_port" "$dump" "vr.opentrack_port = 5555"
        expect "VR client reads pointer_source" "$dump" "vr.pointer_source = head"
    fi
    # lane headinput-finish: mouse mode and Back as Guide write only their keys, and no row but What points
    # ever writes pointer_source or head_dwell_ms (head Point mode never turns on by itself)
    printf '[vr]\nhead_aim = true\n' > "$ini"
    MLXR_LAUNCHER_IW_FILE=/dev/null MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_PAGE=settings \
        MLXR_LAUNCHER_CLICK=mouseModeRow,backAsGuideRow,headLookRow,opentrackRow \
        MLXR_LAUNCHER_SCREENSHOT="$t/settings-mousemode.png@3000" "$L" >"$t/ui13b.out" 2>&1
    grep -q "no clickable" "$t/ui13b.out" && fail "mouse mode click hook: $(grep 'no clickable' "$t/ui13b.out")"
    grep -Eq "$QMLERR" "$t/ui13b.out" && { fail "mouse mode rows: QML errors"; grep -E "$QMLERR" "$t/ui13b.out" | head -3; } || ok "mouse mode and Back as Guide rows load (no QML errors)"
    grep -A8 '^\[vr\]' "$ini" | grep -q '^mouse_mode = relative$' && ok "mouse mode row steps to relative and writes [vr] mouse_mode" || { fail "mouse_mode not written"; cat "$ini"; }
    grep -A8 '^\[vr\]' "$ini" | grep -q '^back_as_guide = true$' && ok "Back as Guide switch writes [vr] back_as_guide = true" || { fail "back_as_guide not written"; cat "$ini"; }
    grep -q '^head_aim = true$' "$ini" && ok "mouse mode rows leave head_aim alone" || { fail "head_aim changed"; cat "$ini"; }
    grep -Eq '^(pointer_source|head_dwell_ms) *=' "$ini" && { fail "a row wrote pointer_source or head_dwell_ms"; cat "$ini"; } || ok "no other row writes pointer_source or head_dwell_ms"
    if [ -n "${MLXR_SETTINGS_DUMP:-}" ] && [ -x "$MLXR_SETTINGS_DUMP" ]; then
        dump="$("$MLXR_SETTINGS_DUMP" "$ini")"
        expect "VR client reads mouse_mode" "$dump" "vr.mouse_mode = relative"
        expect "VR client reads back_as_guide" "$dump" "vr.back_as_guide = true"
        expect "VR client keeps pointer_source on the controllers" "$dump" "vr.pointer_source = controller"
        expect "VR client keeps the dwell off" "$dump" "vr.head_dwell_ms = 0"
    fi
    # lane a11y-tab: the Accessibility section. What points and Dwell click write their keys only when pressed
    # (one press each: Head, 0.6 s), the section's rows are found by the settings search, and a page opened and
    # saved without a press writes neither
    printf '[vr]\nhead_aim = true\n' > "$ini"
    MLXR_LAUNCHER_IW_FILE=/dev/null MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_PAGE=settings \
        MLXR_LAUNCHER_INVOKE='saveAll;setSearch:first-person;setSearch:accessibility' \
        MLXR_LAUNCHER_SCREENSHOT="$SHOTS/settings-accessibility.png@3000" "$L" >"$t/acc0.out" 2>&1
    uiout "$t/acc0.out" "accessibility section"
    grep -Eq '^(pointer_source|head_dwell_ms|assist_dot_size|assist_headset_button|assist_dwell_style) *=' "$ini" && { fail "opening the page wrote an accessibility key"; cat "$ini"; } || ok "the page opened and saved writes no accessibility key"
    s="$(grep 'MLXR_SETTINGS_SEARCH: text="accessibility"' "$t/acc0.out")"
    expect "search: the Accessibility section is found" "$s" "sections=Accessibility"
    expect "search: with its rows" "$s" "titles=Pointer|Dwell click|Dwell click style|Dot size|Headset button clicks sections=Accessibility"
    # lane a11y-2: head-look is under Input with a name that says what it does, Back as Guide under Gamepad
    s="$(grep 'MLXR_SETTINGS_SEARCH: text="first-person"' "$t/acc0.out")"
    expect "search: head-look is found under Input" "$s" "titles=Head turns the game's camera (first-person games) sections=Input Settings"
    MLXR_LAUNCHER_IW_FILE=/dev/null MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_PAGE=settings \
        MLXR_LAUNCHER_CLICK=pointerSourceRow,headDwellRow \
        MLXR_LAUNCHER_INVOKE='setSearch:dwell' MLXR_LAUNCHER_SCREENSHOT="$t/acc1.png@3000" "$L" >"$t/acc1.out" 2>&1
    grep -q "no clickable" "$t/acc1.out" && fail "accessibility click hook: $(grep 'no clickable' "$t/acc1.out")"
    grep -Eq "$QMLERR" "$t/acc1.out" && { fail "accessibility rows: QML errors"; grep -E "$QMLERR" "$t/acc1.out" | head -3; } || ok "accessibility rows load (no QML errors)"
    grep -A8 '^\[vr\]' "$ini" | grep -q '^pointer_source = head$' && ok "What points steps to Head and writes [vr] pointer_source = head" || { fail "pointer_source not written"; cat "$ini"; }
    grep -A8 '^\[vr\]' "$ini" | grep -q '^head_dwell_ms = 600$' && ok "Dwell click steps Off to 0.6 s and writes [vr] head_dwell_ms = 600" || { fail "head_dwell_ms not written"; cat "$ini"; }
    grep -q '^head_aim = true$' "$ini" && ok "accessibility rows leave head_aim alone" || { fail "head_aim changed"; cat "$ini"; }
    expect "search: one word finds the dwell rows" "$(grep 'text="dwell"' "$t/acc1.out")" "titles=Dwell click|Dwell click style|Dot size sections=Accessibility"
    if [ -n "${MLXR_SETTINGS_DUMP:-}" ] && [ -x "$MLXR_SETTINGS_DUMP" ]; then
        dump="$("$MLXR_SETTINGS_DUMP" "$ini")"
        expect "VR client reads pointer_source" "$dump" "vr.pointer_source = head"
        expect "VR client reads head_dwell_ms" "$dump" "vr.head_dwell_ms = 600"
    fi
    # lane a11y-2: the Dot size slider (50 to 300 %, ticks at its 25 % steps) writes [vr] assist_dot_size only when
    # moved, and each Accessibility row has its tip, shown on a gamepad's or a keyboard's focus
    printf '[vr]\nhead_aim = true\n' > "$ini"
    MLXR_LAUNCHER_IW_FILE=/dev/null MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_PAGE=settings \
        MLXR_LAUNCHER_INVOKE='showTip:pointerSourceRow;showTip:headDwellRow;showTip:assistDwellStyleRow;showTip:assistDotSizeRow;showTip:assistHeadsetButtonRow;setPref:assistDotSize|175;saveAll' \
        MLXR_LAUNCHER_SCREENSHOT="$SHOTS/settings-accessibility-tip.png@3000" "$L" >"$t/acc2.out" 2>&1
    uiout "$t/acc2.out" "accessibility tips"
    grep -A8 '^\[vr\]' "$ini" | grep -q '^assist_dot_size = 175$' && ok "Dot size writes [vr] assist_dot_size = 175" || { fail "assist_dot_size not written"; cat "$ini"; }
    grep -Eq '^(pointer_source|head_dwell_ms|assist_headset_button|assist_dwell_style) *=' "$ini" && { fail "the dot size wrote another accessibility key"; cat "$ini"; } || ok "the dot size writes only its key"
    if [ -n "${MLXR_SETTINGS_DUMP:-}" ] && [ -x "$MLXR_SETTINGS_DUMP" ]; then
        dump="$("$MLXR_SETTINGS_DUMP" "$ini")"
        expect "VR client reads assist_dot_size" "$dump" "vr.assist_dot_size = 175"
    fi
    # the headset button writes its key when pressed
    printf '[vr]\nhead_aim = true\n' > "$ini"
    MLXR_LAUNCHER_IW_FILE=/dev/null MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_PAGE=settings \
        MLXR_LAUNCHER_CLICK=assistHeadsetButtonRow MLXR_LAUNCHER_SCREENSHOT="$t/acc3.png@3000" "$L" >"$t/acc3.out" 2>&1
    uiout "$t/acc3.out" "headset button row"
    grep -q "no clickable" "$t/acc3.out" && fail "accessibility click hook: $(grep 'no clickable' "$t/acc3.out")"
    grep -A8 '^\[vr\]' "$ini" | grep -q '^assist_headset_button = true$' && ok "Headset button clicks writes [vr] assist_headset_button = true" || { fail "assist_headset_button not written"; cat "$ini"; }
    if [ -n "${MLXR_SETTINGS_DUMP:-}" ] && [ -x "$MLXR_SETTINGS_DUMP" ]; then
        dump="$("$MLXR_SETTINGS_DUMP" "$ini")"
        expect "VR client reads assist_headset_button" "$dump" "vr.assist_headset_button = true"
    fi
    # lane radial-dwell: Dwell click style steps Simple to Radial menu and writes its key, and only it
    printf '[vr]\nhead_aim = true\n' > "$ini"
    MLXR_LAUNCHER_IW_FILE=/dev/null MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_PAGE=settings \
        MLXR_LAUNCHER_CLICK=assistDwellStyleRow MLXR_LAUNCHER_SCREENSHOT="$t/acc4.png@3000" "$L" >"$t/acc4.out" 2>&1
    uiout "$t/acc4.out" "dwell click style row"
    grep -q "no clickable" "$t/acc4.out" && fail "accessibility click hook: $(grep 'no clickable' "$t/acc4.out")"
    grep -A8 '^\[vr\]' "$ini" | grep -q '^assist_dwell_style = radial$' && ok "Dwell click style steps to Radial menu and writes [vr] assist_dwell_style = radial" || { fail "assist_dwell_style not written"; cat "$ini"; }
    grep -Eq '^(pointer_source|head_dwell_ms|assist_dot_size|assist_headset_button) *=' "$ini" && { fail "the dwell click style wrote another accessibility key"; cat "$ini"; } || ok "the dwell click style writes only its key"
    if [ -n "${MLXR_SETTINGS_DUMP:-}" ] && [ -x "$MLXR_SETTINGS_DUMP" ]; then
        dump="$("$MLXR_SETTINGS_DUMP" "$ini")"
        expect "VR client reads assist_dwell_style" "$dump" "vr.assist_dwell_style = radial"
    fi
    tips="$(grep 'MLXR_SETTINGS_TIP:' "$t/acc2.out")"
    expect "tip: Pointer" "$tips" 'pointerSourceRow visible=true text="What moves the pointer: a controller, your head, or where you look."'
    expect "tip: Dwell click" "$tips" 'headDwellRow visible=true text="Rest the dot this long on something to click it."'
    expect "tip: Dwell click style" "$tips" 'assistDwellStyleRow visible=true text="Radial menu: near the end of the dwell, look left for double click, down to drag, right for right click."'
    expect "tip: Dot size" "$tips" 'assistDotSizeRow visible=true text="How big the dot is when you point with your head or eyes."'
    expect "tip: Headset button" "$tips" "assistHeadsetButtonRow visible=true text=\"The headset's side button clicks instead of opening the Steam home. Hold it 3 seconds to get the Steam home back.\""
    grep -q 'MLXR_LAUNCHER_CLICK\|showTip: no row' "$t/acc2.out" && fail "a tip row is missing: $(grep 'showTip: no row' "$t/acc2.out")"
    # MLXR_HANDOFF=0: the stock probe runs again
    MLXR_HANDOFF=0 MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_SCREENSHOT="$SHOTS/pcs-stock-mode.png@2500" "$L" >"$t/ui4.out" 2>&1
    grep -q "Unable to find working decoder\|Failed to initialize VAAPI" "$t/ui4.out" && ok "hand-off off: stock decoder probe runs" || fail "stock probe missing with MLXR_HANDOFF=0"
    # the segue with a running fake client, then the client failing: dialog
    FAKE_SLEEP=4 FAKE_MODE=err MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_DEMO="gaming-pc:Desktop" \
      MLXR_LAUNCHER_SCREENSHOT="$SHOTS/segue.png@2500,$SHOTS/after.png@7500" "$L" >"$t/ui2.out" 2>&1; rc=$?
    [ $rc = 0 ] && ok "segue demo ran (client started, failed, UI came back)" || fail "segue demo exit $rc"
    grep -E "$QMLERR" "$t/ui2.out" && fail "segue QML errors" || ok "no QML errors in the segue"
    grep -q "The VR client exited with 6" "$t/ui2.out" && ok "client failure seen by the UI" || fail "no exit seen: $(tail -5 "$t/ui2.out")"
    # lane wave4-launcher: the check before the stream finds trouble (the fake PC of section 3e): the segue shows
    # the hints as a toast, and the error dialog of the failed stream repeats them
    srvinfo 50331648 true false SUNSHINE_SERVER_BUSY 1439608285 1 769
    SECTION=mlxr setconf virtual_display=true
    FAKE_SLEEP=4 FAKE_MODE=err MLXR_LAUNCHER_PREFLIGHT=1 MLXR_LAUNCHER_PREFLIGHT_URL="$PFURL" MLXR_NATIVE_CLIENT="$fake" \
      MLXR_LAUNCHER_DEMO="gaming-pc:Desktop" MLXR_LAUNCHER_SCREENSHOT="$SHOTS/segue-hints.png@2000,$SHOTS/after-hints.png@7500" "$L" >"$t/hint.out" 2>&1
    grep -E "$QMLERR" "$t/hint.out" && fail "hints demo QML errors" || ok "hints demo: no QML errors"
    expect "hints: the segue shows them (toast)" "$(grep 'Qt Warning: gaming-pc does not let this client launch apps' "$t/hint.out")" "launch apps"
    expect "hints: the virtual display one too" "$(cat "$t/hint.out")" "Use Virtual Display is on, but Apollo's virtual display driver is not ready"
    grep -q "segue-hints.png\" QSize.* saved" "$t/hint.out" && grep -q "after-hints.png\" QSize.* saved" "$t/hint.out" && ok "hints demo rendered" || fail "hints demo screenshots"
    SECTION=mlxr setconf virtual_display=false
    srvinfo $PERM_ALL true true SUNSHINE_SERVER_FREE 0 1 769
    # lane launcher-return: the window is hidden for the stream (MLXR_STREAMING), shown after it,
    # and the log says when it was exposed and drawn again (outside Game Mode: in it the window fades)
    FAKE_SLEEP=1 FAKE_MODE=stream XDG_CURRENT_DESKTOP=KDE MLXR_LAUNCHER_HIDE_WINDOW=1 MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_DEMO="gaming-pc:Desktop" \
      MLXR_LAUNCHER_SCREENSHOT="$SHOTS/returned.png@6000" "$L" >"$t/ui7.out" 2>&1; rc=$?
    [ $rc = 0 ] && ok "return demo ran" || fail "return demo exit $rc"
    grep -E "$QMLERR" "$t/ui7.out" && fail "return demo QML errors"
    r="$(grep -E "flat window|VR client exited" "$t/ui7.out")"
    echo "$r" | sed 's/^/    /'
    expect "watch starts with the hand-off" "$r" "flat window watched from the hand-off on: visible true"
    expect "hidden for the stream" "$r" "flat window hidden: visible false"
    expect "shown after the stream" "$r" "flat window shown: visible true"
    echo "$r" | grep -Eq "flat window exposed after [0-9]+ ms and drawn$" && ok "exposed and drawn after the stream" || fail "no 'exposed after' line"
    refute "no re-show when it came back" "$r" "showing it again"
    refute "not hidden after the stream" "$r" "hidden again"
    order="$(echo "$r" | grep -n "flat window hidden:\|VR client exited with 0\|flat window shown:\|exposed after" | cut -d: -f2- | sed 's/ - Qt Info//' | tr '\n' '|')"
    case "$order" in *"flat window hidden:"*"VR client exited with 0"*"flat window shown:"*"exposed after"*) ok "hide, exit, show, exposed: in that order";; *) fail "order: $order";; esac
    # a window that does not come back (test hook: the first two checks miss): shown again twice
    FAKE_SLEEP=1 FAKE_MODE=stream XDG_CURRENT_DESKTOP=KDE MLXR_LAUNCHER_HIDE_WINDOW=1 MLXR_LAUNCHER_TEST_RETURN_MISS=2 MLXR_NATIVE_CLIENT="$fake" \
      MLXR_LAUNCHER_DEMO="gaming-pc:Desktop" MLXR_LAUNCHER_SCREENSHOT="$SHOTS/returned-again.png@8000" "$L" >"$t/ui8.out" 2>&1; rc=$?
    [ $rc = 0 ] && ok "missed-return demo ran" || fail "missed-return demo exit $rc"
    r="$(grep -E "flat window|VR client exited" "$t/ui8.out")"
    echo "$r" | sed 's/^/    /'
    [ "$(echo "$r" | grep -c "flat window not exposed, showing it again")" = 2 ] && ok "shown again twice (1 s, then 2 s later)" || fail "re-show count"
    expect "re-show hides first" "$r" "flat window hidden to show it again"
    echo "$r" | grep -Eq "flat window exposed after [0-9]+ ms and drawn \(shown again 2 time\(s\)\)" && ok "back after the second re-show" || fail "no 'exposed after ... shown again 2' line"
    refute "re-show is not reported as a hide after the stream" "$r" "hidden again"
    # Game Mode: the window fades out for the stream and stays mapped (gamescope aborts on a volume
    # key when the hidden window took the keyboard focus with it), fades back in after it
    FAKE_SLEEP=1 FAKE_MODE=stream XDG_CURRENT_DESKTOP=gamescope SteamAppId= MLXR_LAUNCHER_HIDE_WINDOW=1 MLXR_NATIVE_CLIENT="$fake" \
      MLXR_LAUNCHER_DEMO="gaming-pc:Desktop" MLXR_LAUNCHER_SCREENSHOT="$SHOTS/returned-faded.png@6000" "$L" >"$t/ui10.out" 2>&1; rc=$?
    [ $rc = 0 ] && ok "Game Mode return demo ran" || fail "Game Mode return demo exit $rc"
    grep -E "$QMLERR" "$t/ui10.out" && fail "Game Mode return demo QML errors"
    r="$(grep -E "flat window|VR client exited" "$t/ui10.out")"
    echo "$r" | sed 's/^/    /'
    refute "Game Mode: never hidden (unmapped)" "$r" "flat window hidden"
    order="$(echo "$r" | grep -n "faded out for the stream\|VR client exited with 0\|faded back in\|exposed after" | cut -d: -f2- | sed 's/ - Qt Info//' | tr '\n' '|')"
    case "$order" in *"faded out for the stream"*"VR client exited with 0"*"faded back in"*"exposed after"*) ok "Game Mode: fade out, exit, fade in, exposed: in that order";; *) fail "Game Mode order: $order";; esac
    # lane wave2-bind-editor: the Controller bindings page. The launcher has xr/core/xr_bind.c compiled in, so
    # the file is read, checked and its lines found by the headset's own parser; MLXR_LAUNCHER_BIND_DUMP prints
    # the core's serializer output for the file after every load and save, which is what is compared here.
    bf="$MLXR_CONFIG_DIR/bindings.ini"
    bindrun() { # name, then env assignments: opens the page, output in $t/bind-<name>.out
        local n="$1"; shift
        env MLXR_LAUNCHER_IW_FILE=/dev/null MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_PAGE=bindings MLXR_LAUNCHER_BIND_DUMP=1 \
            MLXR_LAUNCHER_WINDOW=1280x1500 "$@" "$L" >"$t/bind-$n.out" 2>&1
    }
    bindcanon() { # the core's serialization of the file, as of the last load or save in that output
        grep 'MLXR_BINDINGS_FILE|' "$1" | awk '/MLXR_BINDINGS_FILE\| begin/ {buf=""; on=1; next} /MLXR_BINDINGS_FILE\| end/ {on=0; last=buf; next} on {sub(/^.*MLXR_BINDINGS_FILE\| ?/, ""); if ($0 != "") buf=buf $0 "\n"} END {printf "%s", last}'
    }
    rawhas() { grep -Fxq -- "$2" "$1" && ok "$3" || { fail "$3: no line [$2] in $1"; cat "$1"; }; }
    rawhasnot() { grep -Fxq -- "$2" "$1" && { fail "$3: line [$2] is there"; cat "$1"; } || ok "$3"; }
    bindqml() { grep -Eq "$QMLERR" "$1" && { fail "$2: QML errors"; grep -E "$QMLERR" "$1" | head -3; } || ok "$2: no QML errors"; }

    # first open: the default file is written (by the core), nothing is wrong with it
    rm -f "$bf"
    bindrun first MLXR_LAUNCHER_SCREENSHOT="$SHOTS/bindings.png@3500"
    bindqml "$t/bind-first.out" "bindings page"
    expect "bindings: the lists the page offers are what the core accepts" "$(cat "$t/bind-first.out")" "MLXR_BINDINGS: choices ok"
    refute "bindings: no choice refused by the core" "$(cat "$t/bind-first.out")" "not accepted by the core"
    [ -f "$bf" ] && ok "bindings: the default file is written on the first open" || fail "no bindings.ini"
    grep -q '^# Moonlight XR controller bindings' "$bf" && grep -q '^\[pointer\]' "$bf" && ok "bindings: it is the core's commented default file" || fail "default file"
    expect "bindings: no errors, no strip, the PCs are listed" "$(cat "$t/bind-first.out")" "errors=0 strip=false pcs="
    expect "bindings: the PC list has the seeded PC" "$(grep MLXR_BINDINGS_UI "$t/bind-first.out")" "gaming-pc"
    grep -q "bindings.png\" QSize.* saved" "$t/bind-first.out" && ok "bindings page rendered" || fail "no bindings screenshot"

    # change one binding: the value on its line is replaced, the comment after it and every other line stay
    printf '# my notes\n[pointer]\nright.x = key:F   ; keep this comment\nleft.trigger = mouse:left\n\n[gamepad]\n# gamepad notes\n' > "$bf"
    bindrun edit MLXR_LAUNCHER_CLICK=row_right.x,kind_mouse,opt_mouse_middle MLXR_LAUNCHER_SCREENSHOT="$SHOTS/bindings-edit.png@3500"
    bindqml "$t/bind-edit.out" "bindings edit"
    grep -q "no clickable" "$t/bind-edit.out" && fail "bindings click hook: $(grep 'no clickable' "$t/bind-edit.out")"
    rawhas "$bf" 'right.x = mouse:middle   ; keep this comment' "bindings: the value changed, the comment after it stayed"
    rawhas "$bf" '# my notes' "bindings: a comment above stayed"
    rawhas "$bf" '# gamepad notes' "bindings: a comment in another section stayed"
    rawhas "$bf" 'left.trigger = mouse:left' "bindings: another binding untouched"
    c="$(bindcanon "$t/bind-edit.out")"
    expect "bindings: the core reads the saved file with the new binding" "$c" $'[pointer]\nright.x = mouse:middle\nleft.trigger = mouse:left'
    refute "bindings: the old binding is gone from the core's reading" "$c" "key:F"
    expect "bindings: the save is logged" "$(cat "$t/bind-edit.out")" "MLXR_BINDINGS: saved pointer right.x = mouse:middle"

    # a binding in the other mode (a new section at the end), a key with modifiers (after its section's last line)
    bindrun more MLXR_LAUNCHER_CLICK=modeGamepad,row_right.grip,kind_pad,opt_pad_rb,modePointer,row_left.dpad_left,kind_key,mod_ctrl,mod_shift,key_esc \
        MLXR_LAUNCHER_SCREENSHOT="$SHOTS/bindings-key.png@3500"
    bindqml "$t/bind-more.out" "bindings more"
    grep -q "no clickable" "$t/bind-more.out" && fail "bindings click hook: $(grep 'no clickable' "$t/bind-more.out")"
    rawhas "$bf" 'right.grip = pad:rb' "bindings: a gamepad binding is written"
    rawhas "$bf" 'left.dpad_left = key:ctrl+shift+esc' "bindings: a key with modifiers is written"
    c="$(bindcanon "$t/bind-more.out")"
    expect "bindings: pointer section as the core reads it" "$c" $'[pointer]\nright.x = mouse:middle\nleft.trigger = mouse:left\nleft.dpad_left = key:ctrl+shift+esc'
    expect "bindings: gamepad section as the core reads it" "$c" $'[gamepad]\nright.grip = pad:rb'

    # one app of a PC's app list: [app "Name"] lines with the mode in front
    bindrun app MLXR_LAUNCHER_CLICK=tabApp,pc_gaming-pc,app_Desktop,row_right.trigger,kind_mouse,opt_mouse_right,modeGamepad,row_right.grip,kind_pad,opt_pad_lb \
        MLXR_LAUNCHER_SCREENSHOT="$SHOTS/bindings-app.png@3500"
    bindqml "$t/bind-app.out" "bindings per app"
    grep -q "no clickable" "$t/bind-app.out" && fail "bindings click hook: $(grep 'no clickable' "$t/bind-app.out")"
    expect "bindings: the PC's apps are the app list" "$(grep 'MLXR_BINDINGS_UI: apps pc=gaming-pc' "$t/bind-app.out")" "Desktop"
    expect "bindings: the PC's apps are the app list (2)" "$(grep 'MLXR_BINDINGS_UI: apps pc=gaming-pc' "$t/bind-app.out")" "Assassin's Creed Origins"
    rawhas "$bf" '[app "Desktop"]' "bindings: the app's section is added"
    rawhas "$bf" 'pointer.right.trigger = mouse:right' "bindings: the app's pointer line"
    rawhas "$bf" 'gamepad.right.grip = pad:lb' "bindings: the app's gamepad line"
    c="$(bindcanon "$t/bind-app.out")"
    expect "bindings: the core reads the app's lines" "$c" $'[app "Desktop"]\npointer.right.trigger = mouse:right\ngamepad.right.grip = pad:lb'

    # reset a row from the page, then the edits the core refuses leave the file as it was
    bindrun reset MLXR_LAUNCHER_CLICK=row_left.dpad_left,resetRow MLXR_LAUNCHER_SCREENSHOT="$t/b.png@2500"
    rawhasnot "$bf" 'left.dpad_left = key:ctrl+shift+esc' "bindings: Reset row takes the line out"
    rawhas "$bf" 'left.trigger = mouse:left' "bindings: Reset row leaves the other lines"
    cp "$bf" "$t/bind.before"
    bindrun refuse MLXR_LAUNCHER_SCREENSHOT="$t/b.png@2500" \
        MLXR_LAUNCHER_BIND_OPS='set|-|pointer|left.view+left.grip|key:F~set|-|pointer|right.x|key:nosuchkey~set|-|pointer|right.a+right.b+left.grip|mouse:left~set|-|gamepad|left.view+right.menu+left.bumper+right.bumper|none~set|-|pointer|left.stick|mouse:left'
    [ "$(grep -c 'MLXR_BINDINGS_OP:.*-> error:' "$t/bind-refuse.out")" = 5 ] && ok "bindings: reserved chords, an unknown key and a button action on a whole stick are refused with the core's error" || { fail "refusals"; grep MLXR_BINDINGS_OP "$t/bind-refuse.out"; }
    expect "bindings: the switch chord's error" "$(cat "$t/bind-refuse.out")" "pointer/gamepad switch shortcut"
    cmp -s "$bf" "$t/bind.before" && ok "bindings: refused edits leave the file byte for byte" || { fail "file changed by refused edits"; diff "$t/bind.before" "$bf"; }

    # a chord, Reset all for an app and for every app: comments and section headers stay
    bindrun chords MLXR_LAUNCHER_SCREENSHOT="$t/b.png@2500" \
        MLXR_LAUNCHER_BIND_OPS='set|-|gamepad|left.view+right.menu|none~set|-|gamepad|right.a+right.b|pad:guide~resetall|Desktop'
    [ "$(grep -c 'MLXR_BINDINGS_OP:.*-> ok' "$t/bind-chords.out")" = 3 ] && ok "bindings: chord edits and Reset all for an app are accepted" || { fail "chord ops"; grep MLXR_BINDINGS_OP "$t/bind-chords.out"; }
    rawhas "$bf" 'left.view+right.menu = none' "bindings: the Guide chord switched off is a line"
    rawhas "$bf" 'right.a+right.b = pad:guide' "bindings: a new chord is a line"
    rawhasnot "$bf" 'pointer.right.trigger = mouse:right' "bindings: Reset all for an app takes its lines out"
    rawhas "$bf" '[app "Desktop"]' "bindings: its section header stays"
    bindrun resetall MLXR_LAUNCHER_SCREENSHOT="$t/b.png@2500" MLXR_LAUNCHER_BIND_OPS='resetall|-'
    c="$(bindcanon "$t/bind-resetall.out")"
    refute "bindings: Reset all (every app) leaves no rules" "$c" " = "
    rawhas "$bf" '# my notes' "bindings: Reset all keeps the comments"
    rawhas "$bf" '# gamepad notes' "bindings: Reset all keeps the other comments"

    # a file with mistakes: the strip lists the core's errors, the bad lines stay through an edit
    printf '[pointer]\nright.x = key:nonsense\nleft.bogus = mouse:left\nright.a = mouse:middle   ; fine\n[gamepad]\nleft.view+left.grip = none\n' > "$bf"
    bindrun errors MLXR_LAUNCHER_CLICK=row_right.b,kind_mouse,opt_mouse_left MLXR_LAUNCHER_SCREENSHOT="$SHOTS/bindings-errors.png@3500"
    bindqml "$t/bind-errors.out" "bindings with errors"
    expect "bindings: the strip is shown for 3 errors" "$(grep MLXR_BINDINGS_UI "$t/bind-errors.out")" "errors=3 strip=true"
    rawhas "$bf" 'right.b = mouse:left' "bindings: an edit works in a file that has errors"
    rawhas "$bf" 'right.x = key:nonsense' "bindings: the line with a bad key is kept for the user to fix"
    rawhas "$bf" 'left.bogus = mouse:left' "bindings: the line with a bad control is kept"
    rawhas "$bf" 'left.view+left.grip = none' "bindings: the reserved chord line is kept"
    c="$(bindcanon "$t/bind-errors.out")"
    expect "bindings: the core still reads the good lines" "$c" $'right.a = mouse:middle\nright.b = mouse:left'

    # CRLF files keep their line ends
    printf '[pointer]\r\nright.x = key:F ; c\r\n' > "$bf"
    bindrun crlf MLXR_LAUNCHER_SCREENSHOT="$t/b.png@2500" MLXR_LAUNCHER_BIND_OPS='set|-|pointer|right.x|mouse:left~set|-|pointer|left.trigger|key:space~set|-|gamepad|left.grip|none'
    python3 -I - "$bf" <<'PY' && ok "bindings: a CRLF file stays CRLF and keeps its comment" || { fail "CRLF file"; od -c "$bf" | head; }
import sys
b = open(sys.argv[1], 'rb').read()
assert b == b'[pointer]\r\nright.x = mouse:left ; c\r\nleft.trigger = key:space\r\n\r\n[gamepad]\r\nleft.grip = none\r\n', b
PY

    # the Settings page opens it
    rm -f "$bf"
    MLXR_LAUNCHER_IW_FILE=/dev/null MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_PAGE=settings MLXR_LAUNCHER_CLICK=bindingsRow \
        MLXR_LAUNCHER_SCREENSHOT="$t/via-settings.png@3000" "$L" >"$t/bind-via.out" 2>&1
    grep -q "no clickable" "$t/bind-via.out" && fail "bindings row: $(grep 'no clickable' "$t/bind-via.out")"
    expect "bindings: the Settings page's row opens the Controller bindings page" "$(cat "$t/bind-via.out")" "MLXR_BINDINGS_UI:"
    bindqml "$t/bind-via.out" "bindings via Settings"
    # lane wave4-launcher: the Stream profiles page (PLAN 4.7), opened as an app's menu opens it (PC 0, app Desktop);
    # the page's own functions make a profile and use it for the app
    MLXR_LAUNCHER_IW_FILE=/dev/null MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_PAGE="profiles:0:Desktop" MLXR_LAUNCHER_WINDOW=1280x1100 \
        MLXR_LAUNCHER_INVOKE='doCreate:Couch;doUse:Couch;doCreate:Default' MLXR_LAUNCHER_SCREENSHOT="$SHOTS/profiles.png@3000" "$L" >"$t/prof1.out" 2>&1
    uiout "$t/prof1.out" "profiles page"
    grep -q "MLXR_LAUNCHER_PAGE: qrc:/gui/XrProfilesView.qml" "$t/prof1.out" && ok "profiles page opened" || fail "no profiles page"
    expect "profiles page: made a profile" "$(cat "$t/prof1.out")" "MLXR_PROFILES_UI: Saved the settings now in force as Couch."
    expect "profiles page: used for the app" "$(cat "$t/prof1.out")" "MLXR_PROFILES_UI: Desktop on gaming-pc uses Couch."
    expect "profiles page: a refused name says why" "$(cat "$t/prof1.out")" "MLXR_PROFILES_UI: error: Default is the settings on the Settings page"
    expect "profiles page: the PC, the app and what it uses" "$(grep 'MLXR_PROFILES_UI: pcs=' "$t/prof1.out")" "pc=gaming-pc app=Desktop apps="
    expect "profiles page: the app list of the PC" "$(grep 'MLXR_PROFILES_UI: pcs=' "$t/prof1.out")" "Assassin's Creed Origins"
    expect "profiles page: assigned and in force" "$(grep 'MLXR_PROFILES_UI: pcs=' "$t/prof1.out")" "profiles=Couch assigned=Couch effective=Couch"
    grep -q "profiles.png\" QSize.* saved" "$t/prof1.out" && ok "profiles page rendered" || fail "no profiles screenshot"
    out="$(pops 'dump')"
    expect "profiles page: the use is saved" "$out" 'use "gaming-pc" / "Desktop" -> "Couch"'
    out="$(pplan "gaming-pc:Desktop")"
    expect "profiles page: the app streams with it" "$out" "SELFTEST: profile: Couch"
    # the other app of the PC has none; the page says so
    MLXR_LAUNCHER_IW_FILE=/dev/null MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_PAGE="profiles:0:Assassin's Creed Origins" \
        MLXR_LAUNCHER_SCREENSHOT="$t/prof2.png@2500" "$L" >"$t/prof2.out" 2>&1
    uiout "$t/prof2.out" "profiles page, another app"
    expect "profiles page: another app uses the Default" "$(grep 'MLXR_PROFILES_UI: pcs=' "$t/prof2.out")" "assigned= effective="
    # the Settings page's row opens it, and an app's menu has the entry
    MLXR_LAUNCHER_IW_FILE=/dev/null MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_PAGE=settings MLXR_LAUNCHER_CLICK=profilesRow \
        MLXR_LAUNCHER_SCREENSHOT="$t/prof3.png@3000" "$L" >"$t/prof3.out" 2>&1
    grep -q "no clickable" "$t/prof3.out" && fail "profiles row: $(grep 'no clickable' "$t/prof3.out")"
    expect "profiles: the Settings page's row opens the Stream profiles page" "$(cat "$t/prof3.out")" "MLXR_PROFILES_UI: pcs="
    uiout "$t/prof3.out" "profiles via Settings"
    av="$(dirname "$L")/src/app/gui/AppView.qml"
    if [ -f "$av" ]; then
        grep -q 'qsTr("Stream settings profile...")' "$av" && grep -q 'stackView.push("qrc:/gui/XrProfilesView.qml", {"initialPcIndex": appGrid.computerIndex, "initialApp": model.name})' "$av" \
            && ok "profiles: an app's menu has Stream settings profile... (hand-off mode)" || fail "app menu entry"
    else
        echo "skip: no $av (app menu check)"
    fi
    pops 'remove|Couch' >/dev/null

    # lane wave4-launcher: search and the jump list on the Settings page (PLAN 4b.17)
    MLXR_LAUNCHER_IW_FILE=/dev/null MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_PAGE=settings MLXR_LAUNCHER_WINDOW=1280x1100 \
        MLXR_LAUNCHER_INVOKE='setSearch:refresh rate' MLXR_LAUNCHER_SCREENSHOT="$SHOTS/settings-search.png@3000" "$L" >"$t/srch1.out" 2>&1
    uiout "$t/srch1.out" "settings search"
    s="$(grep 'MLXR_SETTINGS_SEARCH: text="refresh rate"' "$t/srch1.out")"
    expect "search: the Refresh rate row matches" "$s" "titles=Refresh rate"
    expect "search: only its section is left" "$s" "sections=Video Settings"
    refute "search: other rows are hidden" "$s" "Surround sound"
    grep -q "settings-search.png\" QSize.* saved" "$t/srch1.out" && ok "search rendered" || fail "no search screenshot"
    MLXR_LAUNCHER_IW_FILE=/dev/null MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_PAGE=settings \
        MLXR_LAUNCHER_INVOKE='setSearch:AUDIO;setSearch:virtual display;setSearch:nothing like this;setSearch:' \
        MLXR_LAUNCHER_SCREENSHOT="$t/srch2.png@3000" "$L" >"$t/srch2.out" 2>&1
    uiout "$t/srch2.out" "settings search 2"
    expect "search: a section's name finds its rows, case ignored" "$(grep 'text="AUDIO"' "$t/srch2.out")" "titles=Surround sound configuration|"
    expect "search: and the sections they are in" "$(grep 'text="AUDIO"' "$t/srch2.out")" "sections=Audio Settings|"
    expect "search: two words, both in the row" "$(grep 'text="virtual display"' "$t/srch2.out")" "Use Virtual Display"
    expect "search: nothing found" "$(grep 'text="nothing like this"' "$t/srch2.out")" "rows=0 titles= sections="
    s="$(grep 'MLXR_SETTINGS_SEARCH: text=""' "$t/srch2.out")"
    expect "search: cleared, every section is back" "$s" "sections=Connection|Stream profiles|Video Settings|Audio Settings"
    expect "search: cleared, the last section too" "$s" "|MISC"
    # lane polish-3: the Video bitrate slider is in Connection, right above the per-connection rows whose
    # "Same as above" it is, and no longer under Video Settings
    MLXR_LAUNCHER_IW_FILE=/dev/null MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_PAGE=settings \
        MLXR_LAUNCHER_INVOKE='setSearch:bitrate;setSearch:video settings' \
        MLXR_LAUNCHER_SCREENSHOT="$t/srch3.png@3000" "$L" >"$t/srch3.out" 2>&1
    uiout "$t/srch3.out" "settings search 3"
    s="$(grep 'MLXR_SETTINGS_SEARCH: text="bitrate"' "$t/srch3.out")"
    expect "Video bitrate: first of the bitrate rows, right above the per-connection ones" "$s" \
        "titles=Video bitrate|Bitrate over Wi-Fi|Bitrate over the Steam Frame Wireless Adapter|Bitrate over a wired network|Bitrate over the USB cable"
    expect "Video bitrate: under the Connection heading" "$s" "sections=Connection|"
    [ "$(echo "$s" | grep -o 'Video bitrate' | wc -l)" = 1 ] && ok "Video bitrate: one slider, not two" || fail "Video bitrate twice: $s"
    s="$(grep 'MLXR_SETTINGS_SEARCH: text="video settings"' "$t/srch3.out")"
    expect "search: Video Settings still finds its rows" "$s" "Video resolution|Refresh rate"
    refute "Video bitrate: no longer under Video Settings" "$s" "Video bitrate"
    MLXR_LAUNCHER_IW_FILE=/dev/null MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_PAGE=settings MLXR_LAUNCHER_CLICK=jump_audio \
        MLXR_LAUNCHER_SCREENSHOT="$SHOTS/settings-jump.png@3000" "$L" >"$t/jump.out" 2>&1
    grep -q "no clickable" "$t/jump.out" && fail "jump list: $(grep 'no clickable' "$t/jump.out")"
    uiout "$t/jump.out" "jump list"
    grep -Eq 'MLXR_SETTINGS_JUMP: Audio Settings y=[1-9][0-9]+' "$t/jump.out" && ok "jump list: Audio goes down the page to its section" || fail "jump: $(grep MLXR_SETTINGS_JUMP "$t/jump.out")"
    # lane launcher2: the Game Mode scale (1.5) on a 1920x1080 window. The offscreen screen is
    # 800x800, so this uses QT_SCALE_FACTOR (what the launcher sets in Game Mode) with the window
    # at 1920/1.5 x 1080/1.5 = 1280x720 logical and a windowed UI; the grab is in device pixels.
    # No QML errors, and the grab is 1920x1080 (the window fills the screen at that scale).
    setconf uidisplaymode=0
    for pg in pcs settings; do
        pa=""; [ $pg = settings ] && pa="MLXR_LAUNCHER_PAGE=settings"
        env HAS_DESKTOP_ENVIRONMENT=1 QT_SCALE_FACTOR=1.5 MLXR_LAUNCHER_WINDOW=1280x720 $pa MLXR_LAUNCHER_IW_FILE=/dev/null \
            MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_SCREENSHOT="$SHOTS/scale15-$pg.png@3500" "$L" >"$t/ui9.out" 2>&1
        grep -Eq "$QMLERR" "$t/ui9.out" && { fail "scale 1.5 $pg: QML errors"; grep -E "$QMLERR" "$t/ui9.out" | head -3; } || ok "scale 1.5: $pg page, no QML errors"
        grep -q "scale15-$pg.png\" QSize(1920, 1080) saved" "$t/ui9.out" && ok "scale 1.5: $pg rendered 1920x1080 from a 1280x720 logical window" || fail "scale 1.5 $pg: $(grep Screenshot "$t/ui9.out")"
    done
    # --- lane launcher-bundle, PLAN 6.7: the config.ini keys' rows. A switch clicked, sliders and lists set as the
    # page's own functions do, then only those keys written, as the VR client reads them
    ini="$MLXR_CONFIG_DIR/config.ini"
    printf '[vr]\n# checkbox_vr_show_ray: show the controller ray [true | false] (default true)\n#show_ray = true\nnotices = off\n\n[screen]\ndistance_cm = 280\n' > "$ini"
    MLXR_LAUNCHER_IW_FILE=/dev/null MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_PAGE=settings MLXR_LAUNCHER_CLICK=vr.show_ray,vr.passthrough,display.glow \
        MLXR_LAUNCHER_INVOKE='vrSet:vr.picture_contrast|120;vrSet:vr.picture_gamma|135;vrSet:vr.head_aim_sensitivity|99;vrSet:screen.width_cm|450;vrPick:vr.environment|3;vrPick:display.stats_style|1;vrPick:vr.depth_source|1;vrPick:vr.gamepad_toggle|2;pickOnStart:1;saveAll;setSearch:picture' \
        MLXR_LAUNCHER_SCREENSHOT="$SHOTS/settings-vrkeys.png@3000" "$L" >"$t/vrk.out" 2>&1
    grep -q "no clickable\|no function\|could not call\|vrPick: no row" "$t/vrk.out" && fail "config key rows: $(grep -E 'no clickable|no function|could not call|vrPick' "$t/vrk.out" | head -3)"
    uiout "$t/vrk.out" "config key rows"
    sec() { sed -n "/^\[$1\]/,/^\[/p" "$ini"; }
    expect "a switch row writes its key: Show the ray off" "$(sec vr)" "show_ray = false"
    refute "the commented default line was replaced" "$(cat "$ini")" "#show_ray"
    expect "Passthrough on" "$(sec vr)" "passthrough = true"
    expect "Glow on" "$(sec display)" "glow = true"
    expect "a slider row writes its key: contrast" "$(sec vr)" "picture_contrast = 120"
    expect "gamma in hundredths" "$(sec vr)" "picture_gamma = 135"
    expect "a value past the range is held to it (head aim speed 99 -> 30)" "$(sec vr)" "head_aim_sensitivity = 30"
    expect "screen width" "$(sec screen)" "width_cm = 450"
    expect "a list row writes the VR client's name: environment" "$(sec vr)" "environment = home_theater"
    expect "stats style" "$(sec display)" "stats_style = hud"
    expect "depth model" "$(sec vr)" "depth_source = zipdepth-int8"
    expect "gamepad mode shortcut" "$(sec vr)" "gamepad_toggle = triggers_grips"
    expect "other keys untouched (notices)" "$(sec vr)" "notices = off"
    expect "other keys untouched (screen distance)" "$(sec screen)" "distance_cm = 280"
    refute "rows left alone write nothing" "$(cat "$ini")" "click_sound"
    expect "On start row: Last PC's Desktop" "$(grep '^on_start=' "$CONF")" "on_start=desktop"
    s="$(grep 'MLXR_SETTINGS_SEARCH: text="picture"' "$t/vrk.out")"
    expect "search finds the new rows" "$s" "Picture brightness|Picture contrast|Picture gamma|Picture saturation"
    if [ -n "${MLXR_SETTINGS_DUMP:-}" ] && [ -x "$MLXR_SETTINGS_DUMP" ]; then
        dump="$("$MLXR_SETTINGS_DUMP" "$ini")"
        for kv in "vr.show_ray = false" "vr.passthrough = true" "display.glow = true" "vr.picture_contrast = 120" "vr.picture_gamma = 135" \
                  "vr.head_aim_sensitivity = 30" "screen.width_cm = 450" "vr.environment = home_theater" "display.stats_style = hud" \
                  "vr.depth_source = zipdepth-int8" "vr.gamepad_toggle = triggers_grips" "screen.distance_cm = 280"; do
            expect "VR client reads $kv" "$dump" "$kv"
        done
        expect "VR client: nothing it could not read" "$dump" "unknown 0, unreadable 0"
    fi
    # read back: the switch starts off now, a click turns it on again
    MLXR_LAUNCHER_IW_FILE=/dev/null MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_PAGE=settings MLXR_LAUNCHER_CLICK=vr.show_ray \
        MLXR_LAUNCHER_INVOKE='setSearch:steadiness' MLXR_LAUNCHER_SCREENSHOT="$t/vrk2.png@3000" "$L" >"$t/vrk2.out" 2>&1
    expect "the rows read config.ini back (Show the ray off -> on)" "$(sec vr)" "show_ray = true"
    expect "search: one word finds its row" "$(grep 'text="steadiness"' "$t/vrk2.out")" "rows=1 titles=Click steadiness sections=Input Settings"
    SECTION=mlxr setconf on_start=none

    # --- lane vpn-fallback: Connection's "Use VPN addresses only away from home" is on by default (nothing in the
    # file); a click writes [stream] vpn_only_away = false, which the VR client reads, and a second start reads it back
    printf '[stream]\nfailover = on\n' > "$ini"
    MLXR_LAUNCHER_IW_FILE=/dev/null MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_PAGE=settings MLXR_LAUNCHER_CLICK=stream.vpn_only_away \
        MLXR_LAUNCHER_INVOKE='setSearch:vpn' MLXR_LAUNCHER_SCREENSHOT="$t/vpn.png@3000" "$L" >"$t/vpn.out" 2>&1
    grep -q "no clickable" "$t/vpn.out" && fail "VPN row: $(grep 'no clickable' "$t/vpn.out")"
    uiout "$t/vpn.out" "VPN row"
    expect "VPN row: a click turns it off, written to [stream]" "$(sec stream)" "vpn_only_away = false"
    expect "VPN row: other keys untouched" "$(sec stream)" "failover = on"
    expect "VPN row: search finds it under Connection" "$(grep 'text="vpn"' "$t/vpn.out")" "titles=Use VPN addresses only away from home sections=Connection"
    if [ -n "${MLXR_SETTINGS_DUMP:-}" ] && [ -x "$MLXR_SETTINGS_DUMP" ]; then
        expect "VR client reads stream.vpn_only_away = false" "$("$MLXR_SETTINGS_DUMP" "$ini")" "stream.vpn_only_away = false"
        expect "VR client: on by default" "$("$MLXR_SETTINGS_DUMP" --table)" "stream.vpn_only_away b true"
    fi
    MLXR_LAUNCHER_IW_FILE=/dev/null MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_PAGE=settings MLXR_LAUNCHER_CLICK=stream.vpn_only_away \
        MLXR_LAUNCHER_SCREENSHOT="$t/vpn2.png@3000" "$L" >"$t/vpn2.out" 2>&1
    expect "VPN row: read back off, a click turns it on again" "$(sec stream)" "vpn_only_away = true"

    # --- PLAN 6.2: the update notice in the toolbar (stock's update button) with this project's version
    rel v999.0.0
    MLXR_LAUNCHER_UPDATE=1 MLXR_LAUNCHER_UPDATE_JSON="$t/rel.json" MLXR_NATIVE_CLIENT="$fake" \
        MLXR_LAUNCHER_SCREENSHOT="$SHOTS/pcs-update.png@2500" "$L" >"$t/upd.out" 2>&1
    uiout "$t/upd.out" "update notice"
    expect "update: the toolbar's notice gets the version and page" "$(grep MLXR_LAUNCHER_UPDATE_NOTICE "$t/upd.out")" "999.0.0 $REL/tag/v999.0.0"
    MLXR_HANDOFF=0 MLXR_LAUNCHER_UPDATE=1 MLXR_LAUNCHER_UPDATE_JSON="$t/rel.json" \
        MLXR_LAUNCHER_SCREENSHOT="$t/pcs-update-stock.png@2500" "$L" >"$t/upd2.out" 2>&1
    refute "update: stock mode shows no notice" "$(cat "$t/upd2.out")" "MLXR_LAUNCHER_UPDATE"

    # --- PLAN 6.4 end to end against a Moonlight host on 127.0.0.1 (tests/backend's fake host, MLXR_FAKE_HOST): the
    # launcher pairs with it, the command line's stream "PC" "Desktop" (the "Desktop in VR" Steam shortcut) hands
    # Desktop to the VR client, and On start opens the last PC's Desktop by itself once the PC answers
    if [ -n "${MLXR_FAKE_HOST:-}" ] && [ -x "$MLXR_FAKE_HOST" ]; then
        e2e="$t/e2e"; mkdir -p "$e2e/home/.config/Moonlight XR Native"
        ECONF="$e2e/home/.config/Moonlight XR Native/Moonlight XR Native.conf"
        printf '[General]\nmdns=false\n' > "$ECONF"   # no discovery: only the fake host is asked
        mkfifo "$e2e/pin"
        "$MLXR_FAKE_HOST" < "$e2e/pin" > "$e2e/fake.out" 2>&1 &
        FHPID=$!
        exec 7>"$e2e/pin"
        for _ in $(seq 100); do grep -q '^PORTS' "$e2e/fake.out" && break; sleep 0.1; done
        read -r _ fhttp _ < <(grep '^PORTS' "$e2e/fake.out")
        echo 1234 >&7
        ( export HOME="$e2e/home" MLXR_CONFIG_DIR="$e2e/native" MLXR_NO_SEED=1
          MLXR_LAUNCHER_SCREENSHOT="$e2e/pair.png@6000" "$L" pair "127.0.0.1:$fhttp" --pin 1234 > "$e2e/pair.out" 2>&1
          grep -q '^1\\srvcert=' "$ECONF" && echo paired
          MLXR_LAUNCHER_DRYRUN=1 MLXR_NATIVE_CLIENT= timeout 120 "$L" stream fake-host Desktop > "$e2e/cli.out" 2>&1; echo "cli rc $?"
          grep '^MLXR_LAUNCHER_DRYRUN: ' "$e2e/cli.out" | tail -1
          sed -i 's/^\[mlxr\]$/[mlxr]\non_start=desktop/' "$ECONF"
          grep -q '^on_start=' "$ECONF" || printf '\n[mlxr]\non_start=desktop\n' >> "$ECONF"
          : > "$FAKE_LOG"
          FAKE_SLEEP=1 MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_SCREENSHOT="$SHOTS/onstart-after.png@8000" "$L" > "$e2e/gui.out" 2>&1
          echo "gui rc $?"
        ) > "$e2e/steps" 2>&1
        exec 7>&-; kill "$FHPID" 2>/dev/null; wait "$FHPID" 2>/dev/null
        st="$(cat "$e2e/steps")"
        expect "fake host: the launcher pairs with it" "$st" "paired"
        expect "CLI stream \"PC\" \"Desktop\": the launcher exits after the stream" "$st" "cli rc 0"
        expect "CLI stream \"PC\" \"Desktop\": Desktop goes to the VR client" "$st" "--host FAKE-HOST-UUID-0001 --app 881448767 "
        grep -q '^last_pc=fake-host$' "$ECONF" && ok "the CLI stream's PC is the last PC" || fail "last_pc after the CLI stream: $(grep last_pc "$ECONF")"
        uiout "$e2e/gui.out" "On start"
        expect "On start: waits for the last PC" "$(cat "$e2e/gui.out")" "MLXR_LAUNCHER_ONSTART: waiting for fake-host to start Desktop"
        expect "On start: starts Desktop once it is online" "$(cat "$e2e/gui.out")" "MLXR_LAUNCHER_ONSTART: starting Desktop on fake-host"
        expect "On start: the VR client gets Desktop" "$(cat "$FAKE_LOG")" "--host FAKE-HOST-UUID-0001 --app 881448767 "
        expect "On start: the flat window is back after the stream" "$(cat "$e2e/gui.out")" "The VR client exited with 0"
    else
        echo "note: no MLXR_FAKE_HOST (tests/backend's backend_fake_host_main): the end-to-end On start and CLI checks are skipped"
    fi
fi
echo "failures: $fails"
[ $fails = 0 ]
