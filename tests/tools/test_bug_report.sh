#!/bin/bash
# tools/bug-report.sh must mask addresses, names and ids and keep timings. GPLv3.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
t="$(mktemp -d)"; trap 'rm -rf "$t"' EXIT
mkdir -p "$t/logs" "$t/cfg" "$t/lcfg" "$t/out"
cat > "$t/logs/moonlight.log" <<'LOG'
12:34:56.789 I host test-pc-name answers at 192.168.1.10 (network path home): Sunshine, app version 7.1.431.-1, GfeVersion 3.23.0.74, codecs 0x3, paired, current game 0
12:34:56.790 I network: Wi-Fi (Example Home Net, 5 GHz) via wlan0 10.35.50.1; adapter frame_1A2B3C4D; mdns test-pc-name.local -> fe80::1c2d:3e4f:5a6b:7c8d%wlan0 and 2001:db8:85a3::8a2e:370:7334
12:34:56.791 I candidates: 100.64.0.5 203.0.113.50 10.0.0.5 mac 02:f0:2c:5d:a5:10
12:34:56.792 I gamepad #1 arrived: Wireless Controller (VID/PID 054c/0ce6, guid 050000004c050000e60c000000810000, type 7, caps 0x3, buttons 0x7fff, serial '06:5a:5c:12:34:56'), mapping: none
12:34:56.793 I uniqueid=0123456789abcdef uuid=6E1F0C2A-3B4D-4E5F-8A9B-0C1D2E3F4A5B owner someone@example.com
12:34:56.794 I path /home/someone/.local/share/Steam/userdata/1234/config ssid=Example Home Net
12:34:56.795 I decode 3.7 ms p50, 1234567890123456 ns, frames 162001 lost 68, 90.00 Hz
12:34:56.796 I host session ended on test-pc-name
fingerprint AB:CD:EF:01:23:45:67:89:AB:CD:EF:01:23:45:67:89:AB:CD:EF:01
-----BEGIN CERTIFICATE-----
MIIBszCCAVmg
-----END CERTIFICATE-----
LOG
printf '[6E1F0C2A-3B4D-4E5F-8A9B-0C1D2E3F4A5B]\nname = test-pc-name\naddress = 192.168.1.10\nextra_addresses = 100.64.0.5,10.35.50.27\nmac = 02:f0:2c:5d:a5:10\ncert = servercerts/x.pem\n' > "$t/cfg/hosts.ini"
printf 'host = 192.168.1.10\nbitrate = 50000\nprivate_key = 0123\n' > "$t/cfg/config.ini"
printf '[hosts]\n1\\hostname=other-pc-name\n' > "$t/lcfg/Moonlight XR Native.conf"
HOME="$t/home-tester" MLXR_LOG_DIR="$t/logs" MLXR_CONFIG_DIR="$t/cfg" MLXR_LAUNCHER_CONF_DIR="$t/lcfg" REPORT_DIR="$t/out" \
    "$ROOT/tools/bug-report.sh" > "$t/stdout" 2>&1 || { cat "$t/stdout"; echo "bug-report.sh failed"; exit 1; }
zip="$(ls "$t"/out/*.zip)"
python3 -I -c 'import sys,zipfile; z=zipfile.ZipFile(sys.argv[1]); [sys.stdout.write("== "+n+"\n"+z.read(n).decode()+"\n") for n in z.namelist()]' "$zip" > "$t/all.txt"
fail=0
for s in 192.168.1.10 10.35.50 100.64.0.5 203.0.113.50 10.0.0.5 fe80::1c2d 2001:db8 02:f0:2c 06:5a:5c test-pc-name other-pc-name \
         'Example Home Net' 1A2B3C4D 0123456789abcdef 6E1F0C2A someone@example.com userdata/1234/ AB:CD:EF MIIBsz 'BEGIN CERT' 'private_key = 0123'; do
    grep -qF -- "$s" "$t/all.txt" && { echo "not masked: $s"; fail=1; }
done
for s in '3.7 ms p50' 'frames 162001 lost 68' '90.00 Hz' '1234567890123456 ns' '7.1.431' 'GfeVersion 3.23.0.74' 'bitrate = 50000' '5 GHz' 'VID/PID 054c/0ce6' \
         '<ip1:lan>' ':cgnat>' '<ipv6-1:link-local>' '<host1>' 'name = <host1>' 'session ended on <host1>' '/home/<user>/' 'serial' 'client.key'; do
    case "$s" in client.key) grep -qF "client.key" <(python3 -I -c 'import sys,zipfile; print(zipfile.ZipFile(sys.argv[1]).namelist())' "$zip") && { echo "client.key bundled"; fail=1; };;
        *) grep -qF -- "$s" "$t/all.txt" || { echo "lost: $s"; fail=1; };; esac
done
[ $fail = 0 ] && echo "bug-report masking ok" || { cat "$t/all.txt"; exit 1; }
