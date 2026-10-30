#!/bin/bash
# tools/bug-report.sh must bundle only what is needed, mask addresses, names and ids, keep timings
# and leave out hosts.ini, keys and certificates. GPLv3.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
t="$(mktemp -d)"; trap 'rm -rf "$t"' EXIT
mkdir -p "$t/logs" "$t/cfg" "$t/out" "$t/home-tester/.local/share/Steam/logs"
printf "x vrserver 2.18.2 startup with PID=1, config=/home/someone/.config\n" > "$t/home-tester/.local/share/Steam/logs/vrserver.txt"
USERNAME="$(id -un)"
cat > "$t/logs/moonlight.log.tmpl" <<'LOG'
12:34:56.789 I host test-pc-name answers at 192.168.1.10 (network path home): Sunshine, app version 7.1.431.-1, GfeVersion 3.23.0.74, codecs 0x3, paired, current game 0
12:34:56.790 I network: Wi-Fi (Example Home Net, 5 GHz) via wlan0 10.35.50.1; adapter frame_1A2B3C4D; mdns test-pc-name.local -> fe80::1c2d:3e4f:5a6b:7c8d%wlan0 and 2001:db8:85a3::8a2e:370:7334
12:34:56.791 I candidates: 100.64.0.5 203.0.113.50 10.0.0.5 mac 02:f0:2c:5d:a5:10
12:34:56.792 I gamepad #1 arrived: Wireless Controller (VID/PID 054c/0ce6, guid 050000004c050000e60c000000810000, type 7, caps 0x3, buttons 0x7fff, serial '06:5a:5c:12:34:56'), mapping: none
12:34:56.793 I uniqueid=0123456789abcdef uuid=6E1F0C2A-3B4D-4E5F-8A9B-0C1D2E3F4A5B owner someone@example.com
12:34:56.794 I path /home/someone/.local/share/Steam/userdata/1234/config ssid=Example Home Net
12:34:56.795 I decode 3.7 ms p50, 1234567890123456 ns, frames 162001 lost 68, 90.00 Hz
12:34:56.796 I host session ended on test-pc-name
12:34:56.797 I xr: 72.0 fps (display 72 Hz), video 63.8 fps shown
12:34:56.798 I connecting rtsp://test-pc-name:48010/ and https://other-pc-name:47984/serverinfo?uniqueid=0123456789abcdef; also http://[2001:db8::1]:80/x and test-box-name.local
12:34:56.799 I loaded /home/steamos/.config/moonlight-xr/config.ini, /home/someone/x, /run/user/1000/bus; account id 76561198012345678 user $USERNAME_PLACEHOLDER
12:34:56.800 I hex id deadbeefdeadbeefdeadbeef and machine-id 0123456789abcdef0123456789abcdef
HOME=/home/someone
XDG_RUNTIME_DIR=/run/user/1000
MLXR_VERBOSE=1
fingerprint AB:CD:EF:01:23:45:67:89:AB:CD:EF:01:23:45:67:89:AB:CD:EF:01
-----BEGIN CERTIFICATE-----
MIIBszCCAVmg
-----END CERTIFICATE-----
LOG
sed "s/\$USERNAME_PLACEHOLDER/$USERNAME/" "$t/logs/moonlight.log.tmpl" > "$t/logs/moonlight.log"; rm "$t/logs/moonlight.log.tmpl"
printf '[6E1F0C2A-3B4D-4E5F-8A9B-0C1D2E3F4A5B]\nname = test-pc-name\naddress = 192.168.1.10\nextra_addresses = 100.64.0.5,10.35.50.27\nmac = 02:f0:2c:5d:a5:10\ncert = servercerts/x.pem\n' > "$t/cfg/hosts.ini"
printf '# a comment about the host\n[stream]\nhost = 192.168.1.10\napp = My Game\nbitrate_kbps = 50000\nresolution = 2560x1440\ncodec = hevc\nlast_server_name = test-pc-name\nunique_id = 0123456789abcdef\nprivate_key = 0123\nsave_path = /home/someone/x\nssid = Example Home Net\n[vr]\nstats = true\nkeyboard_layout = compact\nmac_addr = 02:f0:2c:5d:a5:10\ncert = x.pem\n[debug]\nfile_log = verbose\n' > "$t/cfg/config.ini"
# the ray recorder's file (MLXR_RAY_DEBUG): an old one and a newer one, the newer is bundled, header once
ray_row='1000000000,0,0xf,0.0000,0.0000,0.0000,0.00000,0.00000,0.00000,1.00000,3,1,0xf,1,-0.2500,-0.3000,-0.3000'
printf 't_ns,to_head,head_flags,hx,hy,hz\nold-row-not-wanted\n' > "$t/out/ray-20260101-000000.csv"
touch -d '2026-01-01 00:00:00' "$t/out/ray-20260101-000000.csv"
{ printf 't_ns,to_head,head_flags,hx,hy,hz\n'; for i in 1 2 3; do echo "$ray_row"; done; } > "$t/out/ray-20261029-120000.csv"
HOME="$t/home-tester" MLXR_LOG_DIR="$t/logs" MLXR_CONFIG_DIR="$t/cfg" MLXR_EXTRA_NAMES="test-box-name" REPORT_DIR="$t/out" \
    "$ROOT/tools/bug-report.sh" > "$t/stdout" 2>&1 || { cat "$t/stdout"; echo "bug-report.sh failed"; exit 1; }
zip="$(ls "$t"/out/*.zip)"
python3 -I -c 'import sys,zipfile; z=zipfile.ZipFile(sys.argv[1]); [sys.stdout.write("== "+n+"\n"+z.read(n).decode()+"\n") for n in z.namelist()]' "$zip" > "$t/all.txt"
python3 -I -c 'import sys,zipfile; print("\n".join(n.split("/",1)[1] for n in zipfile.ZipFile(sys.argv[1]).namelist()))' "$zip" | sort > "$t/files.txt"
fail=0
# only these files, nothing else
printf 'VERSION\nconfig.ini\nmoonlight.log\nray.csv\nsystem.txt\n' | sort > "$t/want.txt"
diff "$t/want.txt" "$t/files.txt" > /dev/null || { echo "unexpected file list:"; cat "$t/files.txt"; fail=1; }
grep -qiE 'hosts\.ini|\.pem|\.key|uniqueid|servercerts|\.conf|kernel\.txt|SUMMARY' "$t/files.txt" && { echo "forbidden file in the zip"; fail=1; }
# nothing identifying in any file
for s in 192.168.1.10 10.35.50 100.64.0.5 203.0.113.50 10.0.0.5 fe80::1c2d 2001:db8 02:f0:2c 06:5a:5c test-pc-name other-pc-name test-box-name \
         'Example Home Net' 1A2B3C4D 0123456789abcdef deadbeefdeadbeef 6E1F0C2A someone@example.com userdata/1234/ AB:CD:EF MIIBsz 'BEGIN CERT' \
         'private_key' 'My Game' /home/someone /home/steamos /run/user/1000 76561198012345678 'HOME=' 'XDG_RUNTIME_DIR' 'last_server_name' 'save_path' 'unique_id' 'mac_addr' 'cert =' 'comment about'; do
    grep -qF -- "$s" "$t/all.txt" && { echo "not masked: $s"; fail=1; }
done
hn="$(hostname 2>/dev/null)"
case "$hn" in ''|frame|steamdeck|steamframe|localhost) ;; *) grep -qiF -- "$hn" "$t/all.txt" && { echo "host name present: $hn"; fail=1; };; esac
grep -qw -- "$USERNAME" "$t/all.txt" && { echo "user name present: $USERNAME"; fail=1; }
grep -qE '([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}' "$t/all.txt" && { echo "a MAC-like string is left"; fail=1; }
grep -qE '\b[0-9]{1,3}(\.[0-9]{1,3}){3}\b' <(grep -v -e '^== ' -e 'GfeVersion' -e 'app version' -e '^moonlight-xr-steam-frame' "$t/all.txt") && { echo "an IPv4-like string is left"; fail=1; }
grep -qiE '[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}' "$t/all.txt" && { echo "a UUID is left"; fail=1; }
# config.ini: settings values only
cfg="$(python3 -I -c 'import sys,zipfile; z=zipfile.ZipFile(sys.argv[1]); n=[x for x in z.namelist() if x.endswith("/config.ini")][0]; print(z.read(n).decode())' "$zip")"
for k in 'bitrate_kbps = 50000' 'resolution = 2560x1440' 'codec = hevc' 'stats = true' 'keyboard_layout = compact' 'file_log = verbose' '[stream]' '[vr]' '[debug]'; do
    grep -qF -- "$k" <<<"$cfg" || { echo "config.ini lost: $k"; fail=1; }
done
grep -qiE '^[a-z0-9_]*(host|addr|uuid|name|path|token|cert|ssid|secret)[a-z0-9_]* *=|(^|_)(id|key|mac|app)(_| *=)' <<<"$cfg" && { echo "config.ini has an identifying key"; fail=1; }
# the system summary
for k in 'SteamVR: 2.18.2' 'SteamOS:' 'Mesa:' "kernel: $(uname -r)" 'display: 72 Hz' 'moonlight-xr-steam-frame: '; do
    grep -qF -- "$k" "$t/all.txt" || { echo "system.txt lost: $k"; fail=1; }
done
# timings and the shape of the lines stay
for s in '3.7 ms p50' 'frames 162001 lost 68' '90.00 Hz' '1234567890123456 ns' '7.1.431' 'GfeVersion 3.23.0.74' '5 GHz' 'VID/PID 054c/0ce6' \
         '<ip1:lan>' ':cgnat>' '<ipv6-1:link-local>' '<host1>' 'session ended on <host' 'MLXR_VERBOSE=1' '~/.config/moonlight-xr/config.ini' 'serial' '12:34:56.789'; do
    grep -qF -- "$s" "$t/all.txt" || { echo "lost: $s"; fail=1; }
done
ray_in="$(python3 -I -c 'import sys,zipfile; z=zipfile.ZipFile(sys.argv[1]); n=[x for x in z.namelist() if x.endswith("/ray.csv")][0]; print(z.read(n).decode())' "$zip")"
[ "$(grep -c '^t_ns,' <<<"$ray_in")" = 1 ] || { echo "ray.csv: the header is not there exactly once"; fail=1; }
[ "$(grep -c '^1000000000,' <<<"$ray_in")" = 3 ] || { echo "ray.csv: the rows are not all there"; fail=1; }
grep -qF 'old-row-not-wanted' <<<"$ray_in" && { echo "ray.csv is not the newest recording"; fail=1; }
grep -qF 'Look inside' "$t/stdout" || { echo "no reminder printed"; fail=1; }
grep -qF 'moonlight.log' "$t/stdout" || { echo "file list not printed"; fail=1; }
[ $fail = 0 ] && echo "bug-report masking ok" || { cat "$t/all.txt"; exit 1; }
