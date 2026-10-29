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
# frame-rate requests on file descriptors opened by the probe's own v4l2-ctl and
# python3 processes; they are discarded when those processes exit. Each section
# tolerates a missing tool or file and the script always exits 0.
#
# Optional inputs (environment variables):
#   MLXR_DECODER      V4L2 decoder node (default /dev/video-dec0)
#   MLXR_DECODE_TEST  path to an aarch64 mlxr-decode-test binary
#                     (default: PATH, then a few known locations)
#   MLXR_MAIN8_CLIP   path to an 8-bit HEVC Annex-B control clip
#                     (default: ~/moonlight-xr-probe/clips/main8.hevc)
#   MLXR_MAIN10_CLIP  path to a 10-bit HEVC Annex-B clip
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

# attr FILE LABEL [UNIT]: print a sysfs attribute if it is readable.
attr() {
    if [ -f "$1" ] && [ -r "$1" ]; then
        printf '   %s = %s%s\n' "$2" "$(tr -d '\n' < "$1" 2>/dev/null)" "${3:+ $3}"
    fi
}

# unit_of NAME: unit of a power_supply attribute, from its name (kernel ABI
# units: uV, uA, uAh, uW, uWh, percent, 0.1 C for temp, seconds for time_*).
unit_of() {
    case "$1" in
        voltage_*|constant_charge_voltage*|input_voltage_limit) echo uV ;;
        current_*|constant_charge_current*|input_current_limit|precharge_current) echo uA ;;
        charge_now|charge_full|charge_full_design|charge_empty|charge_empty_design|charge_counter) echo uAh ;;
        power_now|power_avg) echo uW ;;
        energy_*) echo uWh ;;
        capacity|capacity_alert_*) echo percent ;;
        temp|temp_*) echo "0.1 C" ;;
        time_*) echo seconds ;;
        cycle_count) echo cycles ;;
    esac
}

# pkg NAME: one pacman -Q per package, so a missing package only changes its own line.
pkg() {
    local out
    out="$(pacman -Q "$1" </dev/null 2>/dev/null)"
    if [ -n "$out" ]; then
        echo "$out"
    else
        echo "not present: package $1"
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
    section "VR processes and decoder holders (a running client can hold the decoder open)"
    if have pgrep; then
        echo "\$ pgrep -fa 'vrserver|vrcompositor|vrclient|moonlight-xr|mlxr' (the probe's own tee is filtered out)"
        pgrep -fa 'vrserver|vrcompositor|vrclient|moonlight-xr|mlxr' </dev/null 2>/dev/null | grep -v ' tee ' \
            || echo "none matched"
    else
        echo "not present: pgrep"
    fi
    if ! have fuser; then
        echo "not present: fuser (cannot tell whether $DEV is held open)"
    elif [ ! -e "$DEV" ]; then
        echo "not present: $DEV"
    else
        echo "\$ fuser $DEV (other users' processes may not show)"
        if fuser "$DEV" </dev/null >/dev/null 2>&1; then
            fuser -v "$DEV" </dev/null 2>&1
            echo "$DEV is held open by the processes above"
        else
            echo "$DEV is not held open by any process fuser can see"
        fi
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
    local f
    for f in $(v4l2-ctl -d "$DEV" --set-fmt-video-out=pixelformat=HEVC --list-formats </dev/null 2>/dev/null \
               | grep -o "'[^']*'" | tr -d "'"); do
        run v4l2-ctl -d "$DEV" --set-fmt-video-out=pixelformat=HEVC --list-framesizes="$f"
    done

    echo
    echo "-- CAPTURE formats by VIDIOC_TRY_FMT (v4l2-ctl cannot name Q10C or P010)"
    echo "   A format the driver does not know comes back as the default NV12 384x256;"
    echo "   a result that keeps 1920x1080 and the asked fourcc means it is advertised."
    if have python3; then
        echo "\$ python3 -I - $DEV  (S_FMT OUTPUT HEVC 1920x1080, then TRY_FMT per CAPTURE format)"
        python3 -I - "$DEV" 2>&1 <<'PY'
import fcntl, os, struct, sys
# Plain V4L2 ioctls on a 64-bit kernel: _IOWR('V', 5 and 64, struct v4l2_format), 208 bytes.
VIDIOC_S_FMT = 0xC0D05605
VIDIOC_TRY_FMT = 0xC0D05640
BUF_OUTPUT_MPLANE = 10
BUF_CAPTURE_MPLANE = 9
dev = sys.argv[1]

def fourcc(s):
    return struct.unpack('<I', s.encode('ascii'))[0]

def fmt(buf_type, w, h, pixfmt):
    # struct v4l2_format: type at 0, the union at 8. In the mplane pix format,
    # width/height/pixelformat/field sit at 8..24, plane_fmt[0] (sizeimage,
    # bytesperline) at 28 and num_planes at 92.
    b = bytearray(208)
    struct.pack_into('<I', b, 0, buf_type)
    struct.pack_into('<IIII', b, 8, w, h, pixfmt, 1)
    return b

try:
    fd = os.open(dev, os.O_RDWR)
except OSError as e:
    print('open %s failed: %s' % (dev, e))
    sys.exit(0)
try:
    b = fmt(BUF_OUTPUT_MPLANE, 1920, 1080, fourcc('HEVC'))
    fcntl.ioctl(fd, VIDIOC_S_FMT, b)
    print('S_FMT OUTPUT HEVC 1920x1080 -> %dx%d' % struct.unpack_from('<II', b, 8))
    for name in ['NV12', 'Q08C', 'Q10C', 'P010', 'NV15', 'NV20', 'P012', 'QC10', 'QC08', 'AB24']:
        b = fmt(BUF_CAPTURE_MPLANE, 1920, 1080, fourcc(name))
        try:
            fcntl.ioctl(fd, VIDIOC_TRY_FMT, b)
        except OSError as e:
            print('TRY_FMT CAPTURE %s failed: %s' % (name, e))
            continue
        w, h, pixfmt = struct.unpack_from('<III', b, 8)
        size, bpl = struct.unpack_from('<II', b, 28)
        got = struct.pack('<I', pixfmt).decode('latin1')
        print('TRY_FMT CAPTURE %s -> %dx%d %s sizeimage %d bytesperline %d planes %d'
              % (name, w, h, got, size, bpl, b[92]))
finally:
    os.close(fd)
PY
        echo "[exit $?]"
    else
        echo "not present: python3 (TRY_FMT check skipped)"
    fi

    echo
    echo "-- frame rate on the OUTPUT queue (ENOTTY means the decoder does not implement it)"
    run v4l2-ctl -d "$DEV" --get-output-parm
    # 30 is the upstream DEFAULT_FPS that the clock vote is sized for (report 09).
    local fps
    for fps in 30 60 90 120; do
        run v4l2-ctl -d "$DEV" --set-fmt-video-out=pixelformat=HEVC --set-output-parm="$fps" --get-output-parm
    done
}

section_decode() {
    section "HEVC clips through mlxr-decode-test"
    local bin="${MLXR_DECODE_TEST:-}" c
    local clip8="${MLXR_MAIN8_CLIP:-$BASE/clips/main8.hevc}"
    local clip10="${MLXR_MAIN10_CLIP:-$BASE/clips/main10.hevc}"
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
    echo "-- what these runs prove: mlxr-decode-test asks the decoder for NV12 or Q08C only, so a"
    echo "   Main 10 clip that passes is a 10-bit stream decoded into an 8-bit surface, not a Q10C"
    echo "   decode. The paced runs (--fps 60) are the result. Lockstep (--fps 0) is informational"
    echo "   only: the Iris HEVC decoder buffers a few access units before it outputs a frame."
    echo "-- paced runs (--fps 60), the 8-bit control first"
    if [ -f "$clip8" ]; then
        echo "clip: $clip8 (8-bit control)"
        tmo 180 "$bin" "$clip8" --codec hevc --fps 60 --progress 10
    else
        echo "not present: 8-bit control clip $clip8 (supply main8.hevc; see README)"
    fi
    if [ -f "$clip10" ]; then
        echo "clip: $clip10 (Main 10)"
        tmo 180 "$bin" "$clip10" --codec hevc --fps 60 --progress 10
        tmo 180 "$bin" "$clip10" --codec hevc --fps 60 --q08c --progress 10
    else
        echo "not present: Main 10 clip $clip10 (supply main10.hevc; see README)"
    fi
    echo "-- lockstep runs (--fps 0), informational: a FAIL here is not a decode failure"
    for c in "$clip8" "$clip10"; do
        if [ -f "$c" ]; then
            tmo 180 "$bin" "$c" --codec hevc --fps 0 --progress 10
        fi
    done
}

section_clinfo() {
    section "OpenCL (clinfo, ICD vendors, rusticl and Mesa OpenCL libraries)"
    tmo 60 clinfo
    echo "-- /etc/OpenCL/vendors"
    if [ -d /etc/OpenCL/vendors ]; then
        ls -la /etc/OpenCL/vendors </dev/null 2>&1
    else
        echo "not present: /etc/OpenCL/vendors"
    fi
    echo "-- installed packages matching opencl"
    if have pacman; then
        pacman -Qs opencl </dev/null 2>&1 || echo "none"
    else
        echo "not present: pacman"
    fi
    echo "-- OpenCL library files"
    local l found=0
    for l in /usr/lib/libOpenCL.so* /usr/lib/libRusticl* /usr/lib/libMesaOpenCL* \
             /usr/lib/*/libRusticl* /usr/lib/*/libMesaOpenCL*; do
        [ -e "$l" ] || continue
        found=1
        ls -l "$l" </dev/null 2>&1
    done
    [ "$found" = 1 ] || echo "no OpenCL library found under /usr/lib (no rusticl, no Mesa OpenCL)"
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
    section "Packages (one pacman -Q per package, so a missing one does not stop the list)"
    if ! have pacman; then
        echo "not present: pacman"
        return 0
    fi
    local p
    for p in qt6-base ffmpeg mesa vulkan-swrast sdl2 qt6-declarative vulkan-freedreno v4l-utils ocl-icd vulkan-tools; do
        pkg "$p"
    done
}

section_usb() {
    section "USB gadget, UDC, Type-C and network"
    echo "-- /sys/class/udc (controllers: state, maximum_speed, function)"
    if [ -d /sys/class/udc ]; then
        ls -la /sys/class/udc </dev/null 2>&1
        local u
        for u in /sys/class/udc/*; do
            [ -e "$u" ] || continue
            echo "   $(basename "$u")"
            attr "$u/state" "state"
            attr "$u/maximum_speed" "maximum_speed"
            attr "$u/function" "function"
        done
    else
        echo "not present: /sys/class/udc"
    fi
    echo "-- /sys/kernel/config/usb_gadget (live gadget tree to depth 3, then every readable file in it)"
    if [ -d /sys/kernel/config/usb_gadget ]; then
        find /sys/kernel/config/usb_gadget -maxdepth 3 </dev/null 2>&1
        local g
        for g in /sys/kernel/config/usb_gadget/*; do
            [ -d "$g" ] || continue
            echo "-- $(basename "$g") attributes"
            find "$g" -maxdepth 3 -type f </dev/null 2>/dev/null | sort | while read -r f; do
                attr "$f" "${f#"$g"/}"
            done
        done
    else
        echo "not present: /sys/kernel/config/usb_gadget"
    fi
    echo "-- loaded modules"
    if have lsmod; then
        local mods
        echo "\$ lsmod | grep -iE 'usb|gadget|ncm|dwc'"
        mods="$(lsmod </dev/null 2>/dev/null | grep -iE 'usb|gadget|ncm|dwc')"
        if [ -n "$mods" ]; then
            printf '%s\n' "$mods"
        else
            echo "no matching modules (built-in drivers do not show in lsmod; see the kernel config below)"
        fi
    else
        echo "not present: lsmod"
    fi
    echo "-- network interfaces and routes"
    run ip -br addr
    run ip -br link
    run ip route
    echo "-- gadget, ADB, NCM and dnsmasq units"
    if have systemctl; then
        echo "\$ systemctl list-units --all --no-pager | grep -iE 'gadget|usb|adb|ncm|dnsmasq'"
        systemctl list-units --all --no-pager </dev/null 2>/dev/null | grep -iE 'gadget|usb|adb|ncm|dnsmasq' \
            || echo "no matching units"
    else
        echo "not present: systemctl"
    fi
    local flag=/etc/systemd/system/adbd.service.d/steamos-devkit-enabled
    if [ -e "$flag" ]; then
        ls -l "$flag" </dev/null 2>&1
    else
        echo "not present: devkit flag $flag"
    fi
    echo "-- Type-C ports and partners (every readable attribute of /sys/class/typec/*)"
    if [ -d /sys/class/typec ]; then
        ls -la /sys/class/typec </dev/null 2>&1
        local t a
        for t in /sys/class/typec/*; do
            [ -d "$t" ] || continue
            echo "   $(basename "$t")"
            for a in "$t"/*; do
                attr "$a" "$(basename "$a")"
            done
        done
    else
        echo "not present: /sys/class/typec"
    fi
    echo "-- kernel config (USB gadget, configfs, NCM/RNDIS/ECM functions, host NCM and RNDIS)"
    if [ -r /proc/config.gz ] && have zcat; then
        local cfg
        cfg="$(zcat /proc/config.gz </dev/null 2>/dev/null \
               | grep -E 'CONFIG_USB_(GADGET|CONFIGFS|F_NCM|F_RNDIS|F_ECM|U_ETHER)|CONFIG_USB_NCM|CONFIG_USB_NET_(CDC_NCM|RNDIS_HOST)')"
        if [ -n "$cfg" ]; then
            printf '%s\n' "$cfg"
        else
            echo "no matching lines (or zcat failed)"
        fi
    else
        echo "not present: /proc/config.gz or zcat"
    fi
    if have lsusb; then
        local lt rc
        lt="$(lsusb -t </dev/null 2>&1)"
        rc=$?
        echo "\$ lsusb -t"
        if [ -n "$lt" ]; then
            printf '%s\n' "$lt"
        else
            echo "no USB devices enumerated (lsusb -t printed nothing; no host is attached to the USB-C port)"
        fi
        echo "[exit $rc]"
    else
        echo "not present: lsusb"
    fi
}

section_battery() {
    section "Battery and power supplies (every readable attribute; units as labelled)"
    if [ ! -d /sys/class/power_supply ]; then
        echo "not present: /sys/class/power_supply"
        return 0
    fi
    local d a
    for d in /sys/class/power_supply/*; do
        [ -e "$d" ] || continue
        echo "-- $(basename "$d")"
        for a in "$d"/*; do
            attr "$a" "$(basename "$a")" "$(unit_of "$(basename "$a")")"
        done
    done
    if have upower; then
        local ups up
        ups="$(upower -e </dev/null 2>/dev/null | grep -i battery)"
        if [ -n "$ups" ]; then
            for up in $ups; do
                run upower -i "$up"
            done
        else
            echo "no battery device listed by upower -e"
        fi
    else
        echo "not present: upower"
    fi
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
             /opt/steamvr/drivers/*/resources/rendermodels \
             "$HOME/.local/share/Steam/steamapps/common/SteamVR/resources/rendermodels" \
             "$HOME/.steam/steam/steamapps/common/SteamVR/resources/rendermodels"; do
        if [ -d "$d" ]; then
            found=1
            echo "-- $d"
            ls -la "$d" </dev/null 2>&1
        fi
    done
    [ "$found" = 1 ] || echo "not present: rendermodels (checked /opt/steamvr/resources, /opt/steamvr/drivers/*/resources and the Steam library)"
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
