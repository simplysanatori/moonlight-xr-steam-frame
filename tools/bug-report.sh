#!/bin/bash
# Collects what is needed to look at a problem into one zip that is safe to share. GPLv3.
#   bug-report.sh
# Writes ~/Games/MoonlightXRNative/reports/mlxr-report-<date>.zip containing:
#   moonlight.log, moonlight.previous.log, moonlight-console.log,
#   moonlight-launcher.log, vrtiming.log    (logs, masked as below)
#   config.ini                              (settings, masked as below)
#   headset-note.txt                        (the newest note from the in-VR "Report a problem" sheet)
#   hosts.ini                               (saved PCs, masked; no certificate or MAC lines)
#   docs/probe/SUMMARY.md                   (platform probe, when found)
#   system.txt                              (version, kernel, SteamOS build id, free space)
#   kernel.txt                              (journalctl -k, last 2 hours, decoder/GPU lines only)
# It never includes client.key, client.pem, uniqueid or servercerts/.
#
# Masked in every file it bundles: IPv4 and IPv6 addresses (each address gets
# a stable token such as <ip2:lan>, so lines about the same address can still
# be matched up), MAC and Bluetooth addresses, host names (the saved PCs, this
# headset, *.local names, "host <name>" in log lines), UUIDs, Moonlight unique
# ids, Wi-Fi network names (SSIDs), serial numbers, e-mail addresses, the
# Steam user id in paths, the home directory, PEM blocks, certificate
# fingerprints and long hex or base64 strings (keys). Timings, counters, codec
# and rate lines are kept. The live logs are not changed: they keep the
# addresses so you can debug your own network.
#
# Environment: MLXR_LOG_DIR (logs, default ~/Games/MoonlightXRNative),
# MLXR_CONFIG_DIR (default ~/.config/moonlight-xr), REPORT_DIR,
# MLXR_LAUNCHER_CONF_DIR (the flat launcher's config, default
# ~/.config/Moonlight XR Native).
set -uo pipefail
HERE="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
LOGDIR="${MLXR_LOG_DIR:-$HOME/Games/MoonlightXRNative}"
CFG="${MLXR_CONFIG_DIR:-$HOME/.config/moonlight-xr}"
LCFG="${MLXR_LAUNCHER_CONF_DIR:-$HOME/.config/Moonlight XR Native}"
OUTDIR="${REPORT_DIR:-$HOME/Games/MoonlightXRNative/reports}"
STAMP="$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUTDIR"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
R="$WORK/mlxr-report-$STAMP"; mkdir -p "$R/docs/probe" "$WORK/raw"

# 1. Copy everything raw into $WORK/raw first; nothing raw goes into $R.
for f in moonlight.log moonlight.log.1 moonlight-console.log moonlight.previous.log moonlight-launcher.log moonlight-launcher.log.1 vrtiming.log vrtiming.log.1; do
    [ -f "$LOGDIR/$f" ] && cp "$LOGDIR/$f" "$WORK/raw/$f"
done
if [ -f "$CFG/config.ini" ]; then
    # values of key-like settings are dropped before the general masking
    sed -E -e 's/^([[:space:]]*[^#;=]*(secret|token|passw|private|cert|pem|(^|[_. -])key[_. -])[^=]*=).*/\1<masked>/I' \
        -e 's/^([[:space:]]*[^#;=]*(^|[_. -])key[[:space:]]*=).*/\1<masked>/I' "$CFG/config.ini" > "$WORK/raw/config.ini"
fi
[ -f "$CFG/hosts.ini" ] && grep -viE '^(cert|mac)[[:space:]]*=' "$CFG/hosts.ini" > "$WORK/raw/hosts.ini"
# the newest note saved from the headset's "Report a problem" sheet
note="$(ls -t "$OUTDIR"/report-*.txt 2>/dev/null | head -n 1)"
[ -n "$note" ] && cp "$note" "$WORK/raw/headset-note.txt"

{
    echo "report: $(date -Is)"
    for v in "$HERE/VERSION" "$HERE/../VERSION"; do [ -f "$v" ] && { echo "moonlight-xr-native: $(cat "$v")"; break; }; done
    { [ -d "$HERE/../.git" ] || [ -f "$HERE/../.git" ]; } && echo "git: $(git -C "$HERE/.." describe --tags --always --dirty 2>/dev/null)"
    echo "kernel: $(uname -srvmo)"
    . /etc/os-release 2>/dev/null && echo "SteamOS: $PRETTY_NAME $VERSION_ID, build id ${BUILD_ID:-unknown}, variant ${VARIANT_ID:-none}"
    echo "binary: $(ls -l "$HERE/moonlight-xr" "$HERE/../build/moonlight-xr" 2>/dev/null | head -n 1)"
    echo "log files:"; ls -l "$LOGDIR"/moonlight*.log 2>/dev/null
    echo "disk:"; df -h "$HOME" 2>/dev/null | tail -n 1
    echo "xr runtime: ${XR_RUNTIME_JSON:-unset}; $(readlink -f "$HOME/.config/openxr/1/active_runtime.json" 2>/dev/null)"
} > "$WORK/raw/system.txt"

{
    if command -v journalctl >/dev/null; then
        journalctl -k --since "-2h" --no-pager 2>&1 | grep -iE 'iris|msm|vidc|smmu|hangcheck|gpu fault'
        echo "(journalctl exit: ${PIPESTATUS[0]}, matching lines: $(journalctl -k --since '-2h' --no-pager 2>/dev/null | grep -ciE 'iris|msm|vidc|smmu|hangcheck|gpu fault'))"
    else
        echo "journalctl not available"
    fi
} > "$WORK/raw/kernel.txt" 2>&1

# Names to mask wherever they appear: this headset's host name, the saved PCs,
# the PCs in the flat launcher's list, the Wi-Fi networks the headset knows.
{
    hostname 2>/dev/null; uname -n
    [ -f "$CFG/hosts.ini" ] && sed -nE 's/^[[:space:]]*name[[:space:]]*=[[:space:]]*(.*)$/\1/p' "$CFG/hosts.ini"
    for c in "$LCFG"/*.conf; do [ -f "$c" ] && sed -nE 's/^[0-9]+\\(hostname|customname)=(.*)$/\2/p' "$c"; done
} | sort -u | grep -vixE '.{0,2}|true|false|localhost|frame|steamdeck|steamos|steamfr(a|)me' > "$WORK/names.txt"   # SteamOS default host names are kept: they are common words in the logs
{
    command -v iw >/dev/null && iw dev 2>/dev/null | sed -nE 's/^[[:space:]]*ssid (.*)$/\1/p'
    command -v nmcli >/dev/null && nmcli -t -f NAME connection show 2>/dev/null
} | sort -u | grep -vxE '|lo' > "$WORK/ssids.txt"

# 2. Mask into $R. One pass over all files, so a token means the same thing everywhere.
python3 -I - "$WORK/raw" "$R" "$WORK/names.txt" "$WORK/ssids.txt" "$HOME" <<'PY'
import ipaddress, os, re, sys
raw, out, names_p, ssids_p, home = sys.argv[1:6]
def lines(p):
    try: return [l.strip() for l in open(p, encoding="utf-8", errors="replace") if l.strip()]
    except OSError: return []
names = sorted(set(lines(names_p)), key=len, reverse=True)
ssids = sorted(set(lines(ssids_p)), key=len, reverse=True)
tokens = {}
def tok(kind, value, label=None):
    key = (kind, value.lower())
    if key not in tokens:
        tokens[key] = "<%s%d%s>" % (kind, sum(1 for k in tokens if k[0] == kind) + 1, ":" + label if label else "")
    return tokens[key]

def v4(m):
    s = m.group(0)
    pre = m.string[max(0, m.start() - 12):m.start()].lower()
    if "version" in pre or "ver " in pre: return s          # 3.23.0.74 in "GfeVersion 3.23.0.74"
    try: a = ipaddress.IPv4Address(s)
    except ValueError: return s
    if a.is_loopback or a.is_unspecified or s == "255.255.255.255": return s
    if a in ipaddress.IPv4Network("100.64.0.0/10"): label = "cgnat"
    elif a.is_link_local: label = "link-local"
    elif a.is_multicast: return s                             # 224.0.0.251 (mDNS) says nothing about anyone
    elif a.is_private: label = "lan"
    else: label = "public"
    return tok("ip", s, label)

def v6(m):
    s = m.group(0)
    if s.count(":") < 2 or not re.search(r"[0-9A-Fa-f]", s): return s
    if "::" not in s and s.count(":") < 7: return s           # 12:34:56 and the like
    try: a = ipaddress.IPv6Address(s.split("%")[0])
    except ValueError: return s
    if a.is_loopback or a.is_unspecified: return s
    return tok("ipv6-", s, "link-local" if a.is_link_local else None)

rules = [
    (re.compile(r"-----BEGIN [A-Z ]+-----.*?-----END [A-Z ]+-----", re.S), lambda m: "<pem block removed>"),
    (re.compile(r"\b[\w.+-]+@[\w-]+(\.[\w-]+)+\b"), lambda m: "<email>"),
    (re.compile(r"\b([0-9A-Fa-f]{2}:){15,}[0-9A-Fa-f]{2}\b"), lambda m: "<fingerprint>"),
    (re.compile(r"\b([0-9A-Fa-f]{2}[:-]){5}[0-9A-Fa-f]{2}\b"), lambda m: "<mac>"),
    (re.compile(r"\b[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}\b"), lambda m: tok("uuid", m.group(0))),
    (re.compile(r"(?<![0-9A-Za-z.])\d{1,3}(\.\d{1,3}){3}(?![0-9A-Za-z])"), v4),
    (re.compile(r"(?<![\w:])[0-9A-Fa-f]{0,4}(:[0-9A-Fa-f]{0,4}){2,7}(%[\w.-]+)?(?![\w:])"), v6),
    (re.compile(r"\b[0-9A-Fa-f]{32,}\b"), lambda m: "<hex>" if re.search("[A-Fa-f]", m.group(0)) else m.group(0)),
    (re.compile(r"(?<![0-9A-Za-z])[0-9A-Fa-f]{16}(?![0-9A-Za-z])"), lambda m: "<id>" if re.search("[A-Fa-f]", m.group(0)) else m.group(0)),
    (re.compile(r"[A-Za-z0-9+/]{64,}={0,2}"), lambda m: m.group(0) if m.group(0).count("/") * 20 > len(m.group(0)) else "<base64>"),
    (re.compile(r"(uniqueid[=: ]+)\S+", re.I), lambda m: m.group(1) + "<id>"),
    (re.compile(r"(serial[\s'=:]+)'?[^\s',)]+'?", re.I), lambda m: m.group(1) + "<serial>"),
    (re.compile(r"(userdata/)\d+"), lambda m: m.group(1) + "<id>"),
    (re.compile(r"\bframe_[0-9A-Fa-f]{6,}\b"), lambda m: "frame_<id>"),
    (re.compile(r"(\bssid[=: ]+)[^\n,;)]+", re.I), lambda m: m.group(1) + "<ssid>"),
    (re.compile(r"(Wi-Fi \()[^,)\n]+"), lambda m: m.group(1) + "<ssid>"),
    (re.compile(r"\b[\w-]+\.local\b"), lambda m: tok("host", m.group(0)[:-6]) + ".local"),
    (re.compile(r"(\bhost(?:name)?[=: ]+)([\w.-]+)(?=\s+(?:answers|not reachable|chosen|is unreachable)|\s*$)", re.I | re.M),
     lambda m: m.group(1) + (m.group(2) if m.group(2).startswith("<") else tok("host", m.group(2)))),
    (re.compile(r"(session ended on )([^<\s]\S*)"), lambda m: m.group(1) + tok("host", m.group(2))),
    (re.compile(r"^(\s*name\s*=\s*)([^<\s].*)$", re.M), lambda m: m.group(1) + tok("host", m.group(2).strip())),
    (re.compile(r"/home/(?!steamos/)[^/\s]+"), lambda m: "/home/<user>"),
]
def mask(text):
    if home and home != "/": text = text.replace(home, "~")
    for n in names:   # whole words only, any case
        text = re.sub(r"(?<![\w-])" + re.escape(n) + r"(?![\w-])", lambda m, n=n: tok("host", n), text, flags=re.I)
    for s in ssids:
        if len(s) >= 3: text = text.replace(s, "<ssid>")
    for rx, fn in rules: text = rx.sub(fn, text)
    return text
for f in sorted(os.listdir(raw)):
    data = open(os.path.join(raw, f), encoding="utf-8", errors="replace").read()
    open(os.path.join(out, f), "w", encoding="utf-8").write(mask(data))
PY
[ $? = 0 ] || { echo "masking failed; nothing written"; exit 1; }

for s in "$HERE/docs/probe/SUMMARY.md" "$HERE/../docs/probe/SUMMARY.md"; do
    [ -f "$s" ] && { cp "$s" "$R/docs/probe/SUMMARY.md"; break; }
done
rmdir "$R/docs/probe" "$R/docs" 2>/dev/null

ZIP="$OUTDIR/mlxr-report-$STAMP.zip"
rm -f "$ZIP"
if command -v zip >/dev/null; then
    ( cd "$WORK" && zip -qr "$ZIP" "mlxr-report-$STAMP" )
else
    python3 -I - "$WORK" "mlxr-report-$STAMP" "$ZIP" <<'PY'
import os, sys, zipfile
work, top, out = sys.argv[1:4]
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for d, _, fs in os.walk(os.path.join(work, top)):
        for f in fs:
            p = os.path.join(d, f)
            z.write(p, os.path.relpath(p, work))
PY
fi
[ -f "$ZIP" ] || { echo "could not write $ZIP"; exit 1; }
echo "wrote $ZIP"
python3 -I -c 'import sys,zipfile; [print("  ",i.filename,i.file_size) for i in zipfile.ZipFile(sys.argv[1]).infolist()]' "$ZIP"
echo "Addresses, host names, network names and ids are masked. Have a quick look before you share it."
