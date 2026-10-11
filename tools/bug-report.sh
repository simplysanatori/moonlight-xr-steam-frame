#!/bin/bash
# Collects the least that is needed to debug a problem into one zip, with nothing that names you. GPLv3.
#   bug-report.sh
# Writes ~/Games/MoonlightXRNative/reports/mlxr-report-<date>.zip containing only:
#   moonlight.log, moonlight.log.1, moonlight.previous.log, moonlight-console.log,
#   moonlight-launcher.log, vrtiming.log     (the app logs, masked as below, last 20000 lines each)
#   headset-note-1.txt ...                   (the newest notes from the in-VR "Report a problem" sheet, masked)
#   config.ini                               (the settings you changed, numbers, on/off and fixed choices only)
#   VERSION
#   system.txt                               (SteamOS, SteamVR, Mesa/Turnip, kernel release, display rate)
# It never includes hosts.ini, client.key, client.pem, uniqueid, servercerts/, the flat
# launcher's Moonlight settings, environment dumps, or the output of ip, iw or nmcli.
#
# Masked in every file it bundles: IPv4 and IPv6 addresses (each address gets a stable
# token such as <ip2:lan>, so lines about the same address can still be matched up),
# MAC addresses, host names (this headset, saved PCs, *.local names, "host <name>" in
# log lines, host names in URLs), the user name (in paths and as a word), UUIDs, Moonlight
# unique ids, hex ids of 16 or more digits, Steam and account ids, e-mail addresses,
# Wi-Fi network names (SSIDs, read from the system and from the logs, including the
# Frame's own access point), serial numbers, PEM blocks, certificate fingerprints and
# base64 keys. Timestamps, timings, counters, codec and rate lines are kept. The live logs
# are not changed.
#
# Environment: MLXR_LOG_DIR (logs, default ~/Games/MoonlightXRNative),
# MLXR_CONFIG_DIR (default ~/.config/moonlight-xr), REPORT_DIR,
# MLXR_EXTRA_NAMES (more names to mask, separated by colons).
set -uo pipefail
HERE="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
LOGDIR="${MLXR_LOG_DIR:-$HOME/Games/MoonlightXRNative}"
CFG="${MLXR_CONFIG_DIR:-$HOME/.config/moonlight-xr}"
OUTDIR="${REPORT_DIR:-$HOME/Games/MoonlightXRNative/reports}"
STAMP="$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUTDIR"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
R="$WORK/mlxr-report-$STAMP"; mkdir -p "$R" "$WORK/raw"

# 1. Copy only what is needed into $WORK/raw first; nothing raw goes into $R.
for f in moonlight.log moonlight.log.1 moonlight.previous.log moonlight-console.log moonlight-launcher.log vrtiming.log; do
    [ -f "$LOGDIR/$f" ] && tail -n 20000 "$LOGDIR/$f" > "$WORK/raw/$f"
done
# settings: only what you changed, and only numbers, on/off and fixed choices
if [ -f "$CFG/config.ini" ]; then
python3 -I - "$CFG/config.ini" "$WORK/raw/config.ini" <<'PY'
import re, sys
src, dst = sys.argv[1:3]
bad_words = {"key", "id", "mac", "app", "user", "pw", "ip", "dns", "url", "uri"}
bad_parts = ("host", "addr", "uuid", "name", "path", "dir", "token", "cert", "pem", "ssid", "serial",
             "secret", "pass", "private", "email", "mail", "login", "account", "steam", "pair", "unique", "domain", "server", "peer")
def key_ok(k):
    k = k.lower()
    if any(p in k for p in bad_parts): return False
    return not (set(re.split(r"[^a-z0-9]+", k)) & bad_words)
def val_ok(v):
    return bool(re.fullmatch(r"-?\d{1,9}(\.\d{1,4})?|\d{3,5}x\d{3,5}|(true|false|yes|no|on|off)|[A-Za-z][A-Za-z0-9_+-]{0,23}", v))
out, sect, pend = [], None, None
for line in open(src, encoding="utf-8", errors="replace"):
    t = line.strip()
    if not t or t[0] in "#;": continue
    m = re.fullmatch(r"\[([A-Za-z0-9_.-]{1,24})\]", t)
    if m: sect = "[%s]" % m.group(1); pend = True; continue
    if "=" not in t: continue
    k, v = (x.strip() for x in t.split("=", 1))
    if key_ok(k) and val_ok(v):
        if pend and sect: out.append(sect); pend = False
        out.append("%s = %s" % (k, v))
open(dst, "w").write("\n".join(out) + "\n")
PY
fi
# the newest notes saved from the headset's "Report a problem" sheet
n=0
for note in $(ls -t "$OUTDIR"/report-*.txt 2>/dev/null | head -n 3); do
    n=$((n+1)); head -c 20000 "$note" > "$WORK/raw/headset-note-$n.txt"
done
for v in "$HERE/VERSION" "$HERE/../VERSION"; do [ -f "$v" ] && { head -n 1 "$v" > "$WORK/raw/VERSION"; break; }; done

# system summary: version strings only
{
    echo "report: $(date -Is)"
    [ -f "$WORK/raw/VERSION" ] && echo "moonlight-xr-steam-frame: $(cat "$WORK/raw/VERSION")"
    ( . /etc/os-release 2>/dev/null && echo "SteamOS: ${VERSION_ID:-unknown}, build ${BUILD_ID:-unknown}" )
    sv="$(grep -m1 -oE 'vrserver [0-9][0-9.]*' "$HOME/.local/share/Steam/logs/vrserver.txt" 2>/dev/null | cut -d' ' -f2)"
    [ -z "$sv" ] && sv="$(grep -m1 -oE '"version" *: *"[0-9][0-9.]*"' /opt/steamvr/bin/version.json 2>/dev/null | grep -oE '[0-9][0-9.]*')"
    echo "SteamVR: ${sv:-unknown}"
    mv_="$(timeout 20 vulkaninfo --summary 2>/dev/null | grep -m1 'driverInfo' | sed -E 's/^[^=]*= *//')"
    [ -z "$mv_" ] && mv_="$(grep -m1 -oE 'Mesa [0-9][0-9A-Za-z.-]*' "$LOGDIR/moonlight.log" 2>/dev/null)"
    echo "Mesa: ${mv_:-unknown}"
    echo "kernel: $(uname -r)"
    hz="$(grep -h -m1 -oE 'display [0-9.]+ Hz' "$LOGDIR/moonlight.log" "$LOGDIR/moonlight.previous.log" 2>/dev/null | head -n 1 | sed 's/^display //')"
    echo "display: ${hz:-not logged}"
} > "$WORK/raw/system.txt" 2>&1

# Names to mask wherever they appear: this headset's host name, the user name, the
# Wi-Fi networks the headset knows. They are read only to be masked; they are not kept.
{
    hostname 2>/dev/null; uname -n
    echo "${MLXR_EXTRA_NAMES:-}" | tr ':' '\n'
} | sort -u | grep -vixE '.{0,2}|true|false|localhost|frame|steamdeck|steamfr(a|)me' > "$WORK/names.txt"
{ id -un 2>/dev/null; echo "${USER:-}"; } | sort -u | grep -vxE '.{0,2}' > "$WORK/users.txt"   # masked as whole words, same case only   # the SteamOS default host names are common words
{
    command -v iw >/dev/null && iw dev 2>/dev/null | sed -nE 's/^[[:space:]]*ssid (.*)$/\1/p'
    if command -v nmcli >/dev/null; then
        nmcli -t -f NAME connection show 2>/dev/null
        nmcli -g SSID device wifi list 2>/dev/null | sed 's/\\:/:/g'
        nmcli -t -f NAME,TYPE connection show 2>/dev/null | sed -nE 's/^(.*):[^:]*wireless$/\1/p' | while IFS= read -r c; do
            nmcli -g 802-11-wireless.ssid connection show "$c" 2>/dev/null
        done
    fi
} | sort -u | grep -vxE '|lo' > "$WORK/ssids.txt"

# 2. Mask into $R. One pass over all files, so a token means the same thing everywhere.
python3 -I - "$WORK/raw" "$R" "$WORK/names.txt" "$WORK/ssids.txt" "$HOME" "$WORK/users.txt" <<'PY'
import ipaddress, os, re, sys
raw, out, names_p, ssids_p, home, users_p = sys.argv[1:7]
def lines(p):
    try: return [l.strip() for l in open(p, encoding="utf-8", errors="replace") if l.strip()]
    except OSError: return []
names = sorted(set(lines(names_p)), key=len, reverse=True)
ssids = sorted(set(lines(ssids_p)), key=len, reverse=True)
users = sorted(set(lines(users_p)), key=len, reverse=True)
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
    (re.compile(r"(?<![0-9A-Za-z])[0-9A-Fa-f]{32,}(?![0-9A-Za-z])"), lambda m: "<hex>" if re.search("[A-Fa-f]", m.group(0)) else m.group(0)),
    (re.compile(r"(?<![0-9A-Za-z])[0-9A-Fa-f]{16,31}(?![0-9A-Za-z])"), lambda m: "<id>" if re.search("[A-Fa-f]", m.group(0)) else m.group(0)),
    (re.compile(r"\b7656119\d{10}\b"), lambda m: "<steamid>"),
    (re.compile(r"\bSTEAM_[0-5]:[01]:\d+\b"), lambda m: "<steamid>"),
    (re.compile(r"(\b(?:account_?id|steam_?id|persona(?:_?name)?|user_?name|login|gamertag|machine_?id|device_?id|android_id|udid)[=: ]+)\S+", re.I), lambda m: m.group(1) + "<id>"),
    (re.compile(r"/run/user/\d+"), lambda m: "/run/user/<uid>"),
    (re.compile(r"\b(\w+)(://)([^/\s@]*@)?(\[[0-9A-Fa-f:.%\w]+\]|[A-Za-z0-9][\w.-]*)"),
     lambda m: m.group(0) if m.group(4).lower() in ("github.com", "api.github.com", "ko-fi.com", "localhost") or m.group(4).startswith("<") or m.group(4).startswith("[<") or m.group(1).lower() in ("file",) else
       m.group(1) + m.group(2) + ("<user>@" if m.group(3) else "") + tok("host", m.group(4))),
    (re.compile(r"^(?!(?:MLXR|MESA|VK|XR|OPENXR|ENABLE|LD|LIBGL|__)\w*=)[A-Z][A-Z0-9_]{2,}=.*$", re.M), lambda m: "<environment line removed>"),
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
    (re.compile(r"/(?:var/)?home/[^/\s]+"), lambda m: "~"),
]
def mask(text):
    if home and home != "/": text = text.replace(home, "~")
    for n in names:   # whole words only, any case
        text = re.sub(r"(?<![\w-])" + re.escape(n) + r"(?![\w-])", lambda m, n=n: tok("host", n), text, flags=re.I)
    for u in users:   # the user name as a word, any path already became ~
        text = re.sub(r"(?<![\w-])" + re.escape(u) + r"(?![\w-])", "<user>", text)
    for s in ssids:
        if len(s) >= 3: text = text.replace(s, "<ssid>")
    for rx, fn in rules: text = rx.sub(fn, text)
    return text
for f in sorted(os.listdir(raw)):
    data = open(os.path.join(raw, f), encoding="utf-8", errors="replace").read()
    open(os.path.join(out, f), "w", encoding="utf-8").write(mask(data))
PY
[ $? = 0 ] || { echo "masking failed; nothing written"; exit 1; }

ZIP="$OUTDIR/mlxr-report-$STAMP.zip"
rm -f "$ZIP"
if command -v zip >/dev/null; then
    ( cd "$WORK" && zip -qrD "$ZIP" "mlxr-report-$STAMP" )
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
echo "Look inside before you share it. Nothing was sent anywhere."
