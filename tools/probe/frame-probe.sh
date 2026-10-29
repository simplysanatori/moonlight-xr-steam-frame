#!/bin/bash
# frame-probe.sh - read-only probe of the Steam Frame for the decoder (plan 1.14)
# and the USB-C gadget network (plan 4c.1). GPLv3.
#
# Run it on the Frame from the PC; see tools/probe/README.md:
#   ssh steamos@<frame-address> 'bash -s' < tools/probe/frame-probe.sh
#
# Output goes to ~/moonlight-xr-probe/<timestamp>/report.txt on the Frame and
# is echoed to stdout. The script changes no system state: no package, config,
# service, network or USB change. Its only device requests are V4L2 format and
# frame-rate requests on the v4l2-ctl process's own file descriptor, which is
# discarded when that process exits. Each section tolerates a missing tool or
# file and the script always exits 0.
#
# Optional inputs (environment variables):
#   MLXR_DECODER      V4L2 decoder node (default /dev/video-dec0)
#   MLXR_DECODE_TEST  path to an aarch64 mlxr-decode-test binary
#                     (default: PATH, then a few known locations)
#   MLXR_MAIN10_CLIP  path to a 10-bit HEVC Annex-B file
#                     (default: ~/moonlight-xr-probe/clips/main10.hevc)
# Nothing is downloaded.

export LC_ALL=C
DEV="${MLXR_DECODER:-/dev/video-dec0}"
BASE="$HOME/moonlight-xr-probe"
STAMP="$(date +%Y%m%d-%H%M%S 2>/dev/null || echo unknown)"
OUT="$BASE/$STAMP"
mkdir -p "$OUT" 2>/dev/null || {
    OUT="${TMPDIR:-/tmp}/moonlight-xr-probe-$STAMP"
    mkdir -p "$OUT" 2>/dev/null
}
REPORT="$OUT/report.txt"

have() { command -v "$1" >/dev/null 2>&1; }

section() { printf '\n==== %s ====\n' "$1"; }

# run CMD [ARGS...]: run a command with stdin closed (the script itself arrives
# on stdin over ssh), print the command, its output and its exit status.
run() {
    if ! have "$1"; then
        echo "not present: $1"
        return 0
    fi
    echo "\$ $*"
    "$@" </dev/null 2>&1
    echo "[exit $?]"
    return 0
}

# tmo SECONDS CMD [ARGS...]: like run, but with a time limit when timeout exists.
tmo() {
    local secs="$1"; shift
    if ! have "$1"; then
        echo "not present: $1"
        return 0
    fi
    echo "\$ $*  (limit ${secs}s)"
    if have timeout; then
        timeout "$secs" "$@" </dev/null 2>&1
    else
        "$@" </dev/null 2>&1
    fi
    echo "[exit $?]"
    return 0
}

# attr FILE LABEL: print a sysfs attribute if it is readable.
attr() {
    if [ -f "$1" ] && [ -r "$1" ]; then
        printf '   %s = %s\n' "$2" "$(tr -d '\n' < "$1" 2>/dev/null)"
    fi
}

section_system() {
    section "System"
    echo "stamp: $STAMP"
    echo "report: $REPORT"
    run uname -a
    if [ -r /etc/os-release ]; then
        grep -E '^(NAME|VERSION_ID|BUILD_ID|VARIANT_ID)=' /etc/os-release
    else
        echo "not present: /etc/os-release"
    fi
    run nproc
}

section_busy() {
    section "VR processes (a running client can hold the decoder open)"
    if have pgrep; then
        echo "\$ pgrep -fa 'vrserver|vrcompositor|vrclient|moonlight-xr|mlxr'"
        pgrep -fa 'vrserver|vrcompositor|vrclient|moonlight-xr|mlxr' </dev/null 2>&1 || echo "none matched"
    else
        echo "not present: pgrep"
    fi
}

section_decoder() {
    section "Video decoder ($DEV)"
    run v4l2-ctl --list-devices
    if [ ! -e "$DEV" ]; then
        echo "not present: $DEV"
        return 0
    fi
    run ls -l "$DEV"
    if ! have v4l2-ctl; then
        echo "not present: v4l2-ctl, decoder section skipped"
        return 0
    fi

    echo
    echo "-- H.264 state (as in the 2026-10-10 probe, for comparison)"
    run v4l2-ctl -d "$DEV" --list-formats-out
    run v4l2-ctl -d "$DEV" --list-formats

    # Each v4l2-ctl call is a new file descriptor, so the OUTPUT format is set
    # again in every call that needs the HEVC state.
    echo
    echo "-- HEVC state (OUTPUT set to HEVC on this fd only; see plan 1.14 and report 09)"
    run v4l2-ctl -d "$DEV" --set-fmt-video-out=pixelformat=HEVC --all
    run v4l2-ctl -d "$DEV" --set-fmt-video-out=pixelformat=HEVC --list-formats
    run v4l2-ctl -d "$DEV" --set-fmt-video-out=pixelformat=HEVC --list-framesizes=HEVC

    echo
    echo "-- framesizes for each CAPTURE format, HEVC state"
    if have v4l2-ctl; then
        local f
        for f in $(v4l2-ctl -d "$DEV" --set-fmt-video-out=pixelformat=HEVC --list-formats </dev/null 2>/dev/null \
                   | grep -o "'[^']*'" | tr -d "'"); do
            run v4l2-ctl -d "$DEV" --set-fmt-video-out=pixelformat=HEVC --list-framesizes="$f"
        done
    fi

    echo
    echo "-- frame rate (S_PARM on OUTPUT; ENOTTY means the decoder has none)"
    run v4l2-ctl -d "$DEV" --get-parm
    # 30 is the upstream DEFAULT_FPS that the clock vote is sized for (report 09).
    run v4l2-ctl -d "$DEV" --set-fmt-video-out=pixelformat=HEVC --set-parm=30 --get-parm
}

section_decode() {
    section "Main10 HEVC through mlxr-decode-test"
    local bin="${MLXR_DECODE_TEST:-}" c clip="${MLXR_MAIN10_CLIP:-$BASE/clips/main10.hevc}"
    if [ -z "$bin" ]; then
        bin="$(command -v mlxr-decode-test 2>/dev/null)"
    fi
    if [ -z "$bin" ]; then
        for c in "$BASE/bin/mlxr-decode-test" "$HOME/Games/MoonlightXRNative/app/mlxr-decode-test" "$BASE/mlxr-decode-test"; do
            if [ -x "$c" ]; then
                bin="$c"
                break
            fi
        done
    fi
    if [ -z "$bin" ] || [ ! -x "$bin" ]; then
        echo "not present: mlxr-decode-test (set MLXR_DECODE_TEST or copy it to $BASE/bin/; see README)"
        return 0
    fi
    echo "binary: $bin"
    tmo 30 "$bin" --probe-modifiers
    if [ ! -f "$clip" ]; then
        echo "not present: Main10 clip $clip (supply one; see README, nothing is downloaded)"
        return 0
    fi
    echo "clip: $clip"
    tmo 180 "$bin" "$clip" --codec hevc --fps 0 --progress 10
    tmo 180 "$bin" "$clip" --codec hevc --fps 0 --q08c --progress 10
}

section_clinfo() {
    section "OpenCL (rusticl)"
    tmo 60 clinfo
}

section_fonts() {
    section "Chinese fonts (fc-list :lang=zh)"
    if have fc-list; then
        echo "\$ fc-list ':lang=zh' family file"
        fc-list ':lang=zh' family file </dev/null 2>&1 | sort | head -200
        echo "count: $(fc-list ':lang=zh' family file </dev/null 2>/dev/null | wc -l)"
    else
        echo "not present: fc-list"
    fi
}

section_packages() {
    section "Packages"
    run pacman -Q qt6-base ffmpeg mesa vulkan-swrast sdl2 qt6-declarative
}

section_usb() {
    section "USB gadget, UDC, Type-C and network"
    echo "-- /sys/class/udc"
    if [ -d /sys/class/udc ]; then
        ls -la /sys/class/udc </dev/null 2>&1
    else
        echo "not present: /sys/class/udc"
    fi
    echo "-- /sys/kernel/config/usb_gadget (configfs)"
    if [ -d /sys/kernel/config/usb_gadget ]; then
        ls -la /sys/kernel/config/usb_gadget </dev/null 2>&1
    else
        echo "not present: /sys/kernel/config/usb_gadget"
    fi
    echo "-- network interfaces"
    run ip -br addr
    run ip -br link
    echo "-- Type-C ports"
    if [ -d /sys/class/typec ]; then
        ls -la /sys/class/typec </dev/null 2>&1
        local f found=0
        for f in /sys/class/typec/*/data_role /sys/class/typec/*/power_role \
                 /sys/class/typec/*/power_operation_mode /sys/class/typec/*/usb_power_delivery_revision; do
            [ -f "$f" ] || continue
            found=1
            printf '%s: %s\n' "$f" "$(tr -d '\n' < "$f" 2>/dev/null)"
        done
        [ "$found" = 1 ] || echo "no typec attribute files found"
    else
        echo "not present: /sys/class/typec"
    fi
    echo "-- kernel config (USB_GADGET, CONFIGFS, NCM, RNDIS, ECM)"
    if [ -r /proc/config.gz ] && have zcat; then
        local cfg
        cfg="$(zcat /proc/config.gz 2>/dev/null | grep -E 'USB_GADGET|CONFIGFS|NCM|RNDIS|ECM')"
        if [ -n "$cfg" ]; then
            printf '%s\n' "$cfg"
        else
            echo "no matching lines (or zcat failed)"
        fi
    else
        echo "not present: /proc/config.gz or zcat"
    fi
    run lsusb -t
}

section_battery() {
    section "Battery and power supplies"
    if [ ! -d /sys/class/power_supply ]; then
        echo "not present: /sys/class/power_supply"
        return 0
    fi
    local d a
    for d in /sys/class/power_supply/*; do
        [ -e "$d" ] || continue
        echo "-- $(basename "$d")"
        for a in type status capacity voltage_now current_now power_now \
                 energy_now energy_full charge_now charge_full present online; do
            attr "$d/$a" "$a"
        done
    done
}

section_wifi() {
    section "Wi-Fi power save"
    if ! have iw; then
        echo "not present: iw"
        return 0
    fi
    run iw dev
    local i found=0
    for i in $(iw dev </dev/null 2>/dev/null | awk '$1=="Interface"{print $2}'); do
        found=1
        run iw dev "$i" get power_save
    done
    [ "$found" = 1 ] || echo "no wireless interfaces listed"
}

section_rendermodels() {
    section "SteamVR render models"
    local d found=0
    for d in /opt/steamvr/resources/rendermodels \
             "$HOME/.local/share/Steam/steamapps/common/SteamVR/resources/rendermodels" \
             "$HOME/.steam/steam/steamapps/common/SteamVR/resources/rendermodels"; do
        if [ -d "$d" ]; then
            found=1
            echo "-- $d"
            ls -la "$d" </dev/null 2>&1
        fi
    done
    [ "$found" = 1 ] || echo "not present: rendermodels (checked /opt/steamvr and the Steam library)"
}

main() {
    echo "frame-probe (plan 1.14 and 4c.1), read-only"
    section_system
    section_busy
    section_decoder
    section_decode
    section_clinfo
    section_fonts
    section_packages
    section_usb
    section_battery
    section_wifi
    section_rendermodels
    section "Done"
    echo "report: $REPORT"
}

main 2>&1 | tee "$REPORT"
exit 0
