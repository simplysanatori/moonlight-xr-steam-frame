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
  pcvr) exit 12;;
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

# --- 4b. lane wave4-pcvr: exit code 12 (the Host sheet's Switch to PC VR) hands the headset to Valve's
# Steam Link stream through steam_cef.py. A fake CEF endpoint (tests/tools/fake_cef.py) stands in for
# Steam; it logs every expression it is asked to evaluate.
SC="$here/../../packaging/steam_cef.py"
FC="$here/../tools/fake_cef.py"
if [ -f "$SC" ] && [ -f "$FC" ]; then
    read -r CEFPORT DEADPORT < <(python3 -I -c 'import socket
a=socket.socket(); a.bind(("127.0.0.1",0)); b=socket.socket(); b.bind(("127.0.0.1",0))
print(a.getsockname()[1], b.getsockname()[1])')
    cat >"$t/cef-rules.json" <<'JSON'
[
 ["navigator.userAgent", {"StreamGame":"function","RunGame":"function","RegisterForDevicesChanges":"function","ExecuteSteamURL":"function","ShowDashboard":"function","userAgent":"Steam/test"}],
 ["RegisterForDevicesChanges", {"devices":[{"clientId":"76561198000000001","clientName":"gaming-pc","unStreamingSessionID":0}]}],
 ["appStore", {"apps":[
   {"appid":620980,"name":"Beat Saber","categories":[53],"perClient":[{"clientid":"76561198000000001","installed":true}]},
   {"appid":440,"name":"Team Fortress 2","categories":[1],"perClient":[{"clientid":"76561198000000001","installed":true}]}]}],
 ["StreamGame", true],
 ["ExecuteSteamURL", true]
]
JSON
    : > "$t/cef.log"
    python3 -I "$FC" "$CEFPORT" "$t/cef-rules.json" "$t/cef.log" >"$t/cef.out" 2>&1 &
    CEFPID=$!
    trap 'kill $CEFPID ${fakepc:-} 2>/dev/null; rm -rf "$t"' EXIT   # (lane wave4-launcher: and the fake PC of 3e)
    for _ in $(seq 1 50); do grep -q "fake CEF on" "$t/cef.out" 2>/dev/null && break; sleep 0.1; done
    export MLXR_STEAM_CEF="$SC" STEAM_CEF_PORT="$CEFPORT" STEAM_CEF_FLAG_FILE="$t/no-flag"
    pcvrsay() { MLXR_LAUNCHER_SELFTEST="$1" "$L" 2>"$t/stderr"; }

    # the exit code, as a dry run: the launcher treats 12 like 11 (no dialog) and prints what it asks Steam
    unset FAKE_MODE
    MLXR_LAUNCHER_DRYRUN="fail:12:" selftest "gaming-pc:Desktop" >"$t/stdout"; rc=$?
    [ $rc = 0 ] && ok "exit 12 (Switch to PC VR) is not an error" || fail "exit 12 gave $rc"
    expect "exit 12: no error dialog" "$(cat "$t/stdout")" "error=none"
    expect "exit 12 with no game chosen: --pcvr alone" "$(cat "$t/stdout")" "MLXR_PCVR: python3 -I steam_cef.py --pcvr"
    refute "exit 12 with no game chosen: no --app" "$(grep '^MLXR_PCVR' "$t/stdout")" "--app"
    SECTION=mlxr setconf pcvr_app=620980 pcvr_client=76561198000000001
    MLXR_LAUNCHER_DRYRUN="fail:12:" selftest "gaming-pc:Desktop" >"$t/stdout"
    expect "exit 12 with a game and a PC chosen" "$(cat "$t/stdout")" "MLXR_PCVR: python3 -I steam_cef.py --pcvr --app 620980 --client 76561198000000001"
    MLXR_LAUNCHER_DRYRUN="fail:11:" selftest "gaming-pc:Desktop" >"$t/stdout"
    refute "exit 11 (disconnect) does not hand off to PC VR" "$(cat "$t/stdout")" "MLXR_PCVR"
    MLXR_LAUNCHER_DRYRUN="fail:6:no" selftest "gaming-pc:Desktop" >"$t/stdout"
    refute "a failing client does not hand off to PC VR" "$(cat "$t/stdout")" "MLXR_PCVR"

    # for real: a client that exits 12, Steam's side played by the fake endpoint
    : > "$t/cef.log"
    FAKE_MODE=pcvr MLXR_NATIVE_CLIENT="$fake" selftest "gaming-pc:Desktop" >"$t/stdout"; rc=$?
    [ $rc = 0 ] && ok "client exit 12 -> success" || fail "client exit 12 gave $rc"
    expect "client exit 12: finished without an error" "$(cat "$t/stdout")" "started=yes finished=yes error=none"
    expect "client exit 12: Steam is asked to StreamGame the chosen game from the chosen PC" "$(cat "$t/cef.log")" 'SteamClient.Apps.StreamGame(620980, "76561198000000001", 0)'
    refute "client exit 12: the flat screen is not asked for" "$(cat "$t/cef.log")" "ShowDashboard"
    # no PC chosen: the only paired PC is used (the script asks Steam for the list first)
    : > "$t/cef.log"
    SECTION=mlxr setconf pcvr_client=
    FAKE_MODE=pcvr MLXR_NATIVE_CLIENT="$fake" selftest "gaming-pc:Desktop" >"$t/stdout"
    expect "no PC chosen: Steam is asked for the paired PCs" "$(cat "$t/cef.log")" "RegisterForDevicesChanges"
    expect "no PC chosen: the only paired PC is streamed from" "$(cat "$t/cef.log")" 'SteamClient.Apps.StreamGame(620980, "76561198000000001", 0)'
    # no game chosen: Steam's library opens
    : > "$t/cef.log"
    SECTION=mlxr setconf pcvr_app=
    FAKE_MODE=pcvr MLXR_NATIVE_CLIENT="$fake" selftest "gaming-pc:Desktop" >"$t/stdout"
    expect "no game chosen: Steam's library opens" "$(cat "$t/cef.log")" "steam://nav/games"
    refute "no game chosen: nothing is streamed" "$(cat "$t/cef.log")" "StreamGame"
    # Steam's port does not answer: the hand-off fails quietly (logged), the launcher carries on
    SECTION=mlxr setconf pcvr_app=620980 pcvr_client=76561198000000001
    STEAM_CEF_PORT="$DEADPORT" FAKE_MODE=pcvr MLXR_NATIVE_CLIENT="$fake" selftest "gaming-pc:Desktop" >"$t/stdout"; rc=$?
    [ $rc = 0 ] && expect "Steam port dead: no error dialog" "$(cat "$t/stdout")" "error=none" || fail "dead port gave $rc"
    expect "Steam port dead: logged with the way to check" "$(cat "$t/stderr")" "steam_cef.py --probe"

    # the preflight and the lists (steam_cef.py through the launcher)
    out="$(pcvrsay pcvr:probe)"
    expect "preflight: port answers, calls present" "$out" "SELFTEST: pcvr probe rc=0"
    expect "preflight: reports ok" "$out" '"ok": true'
    out="$(STEAM_CEF_PORT="$DEADPORT" pcvrsay pcvr:probe)"
    expect "preflight: a dead port is exit 3" "$out" "SELFTEST: pcvr probe rc=3"
    expect "preflight: with a hint" "$out" "does not answer"
    out="$(pcvrsay pcvr:devices)"
    expect "paired PCs are listed" "$out" '"clientName": "gaming-pc"'
    out="$(pcvrsay pcvr:apps:76561198000000001)"
    expect "the PC's games are listed" "$out" '"name": "Beat Saber"'
    expect "VR games first" "$out" '[{"appid": 620980'
    out="$(pcvrsay pcvr:bogus)"; [ $? = 20 ] && ok "a bad pcvr self-test is reported" || fail "bad pcvr self-test"
    unset MLXR_STEAM_CEF STEAM_CEF_PORT STEAM_CEF_FLAG_FILE
    SECTION=mlxr setconf pcvr_app= pcvr_client=
else
    ok "(PC VR tests skipped: no packaging/steam_cef.py or tests/tools/fake_cef.py)"
fi

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
    # lane wave4-pcvr: the Settings page's PC VR rows and the startup preflight
    if [ -n "${CEFPID:-}" ]; then
        # preflight at start (MLXR_PCVR_PREFLIGHT stands in for Game Mode): logged as ok
        MLXR_STEAM_CEF="$SC" STEAM_CEF_PORT="$CEFPORT" STEAM_CEF_FLAG_FILE="$t/no-flag" MLXR_PCVR_PREFLIGHT=1 MLXR_LAUNCHER_IW_FILE=/dev/null \
            MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_PAGE=settings MLXR_LAUNCHER_SCREENSHOT="$t/pcvr-pre.png@3000" "$L" >"$t/ui10.out" 2>&1
        expect "preflight at start: Steam's port is fine" "$(grep 'Steam CEF preflight' "$t/ui10.out")" "Steam CEF preflight: ok"
        MLXR_STEAM_CEF="$SC" STEAM_CEF_PORT="$DEADPORT" STEAM_CEF_FLAG_FILE="$t/no-flag" MLXR_PCVR_PREFLIGHT=1 MLXR_LAUNCHER_IW_FILE=/dev/null \
            MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_PAGE=settings MLXR_LAUNCHER_SCREENSHOT="$t/pcvr-dead.png@3000" "$L" >"$t/ui11.out" 2>&1
        expect "preflight at start: a dead port is logged with the hint" "$(grep 'Steam CEF preflight' "$t/ui11.out")" "NOT OK - Steam's debugging port $DEADPORT does not answer"
        # the Settings page: the rows load without QML errors, and the first one asks Steam for the lists
        MLXR_STEAM_CEF="$SC" STEAM_CEF_PORT="$CEFPORT" STEAM_CEF_FLAG_FILE="$t/no-flag" MLXR_LAUNCHER_IW_FILE=/dev/null \
            MLXR_NATIVE_CLIENT="$fake" MLXR_LAUNCHER_PAGE=settings MLXR_LAUNCHER_SCROLL=1500 MLXR_LAUNCHER_CLICK=pcvrCheckRow \
            MLXR_LAUNCHER_SCREENSHOT="$SHOTS/settings-pcvr.png@4500" "$L" >"$t/ui12.out" 2>&1
        grep -Eq "$QMLERR" "$t/ui12.out" && { fail "PC VR rows: QML errors"; grep -E "$QMLERR" "$t/ui12.out" | head -3; } || ok "settings page with the PC VR rows loads (no QML errors)"
        grep -q "no clickable" "$t/ui12.out" && fail "PC VR click hook: $(grep 'no clickable' "$t/ui12.out")"
        expect "PC VR check row: asks Steam" "$(grep 'Steam CEF preflight' "$t/ui12.out")" "Steam CEF preflight: ok"
        SECTION=mlxr setconf pcvr_app= pcvr_client=
    fi
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
    uiout() { grep -E "$QMLERR" "$1" && fail "$2: QML errors" || ok "$2: no QML errors"; }
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
fi
echo "failures: $fails"
[ $fails = 0 ]
