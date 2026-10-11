#!/bin/bash
# Looks for personal data before anything is published. GPLv3.
#   tools/privacy-check.sh            the tracked files (git ls-files), third_party/ excluded
#   tools/privacy-check.sh DIR|TAR    every file under DIR, or inside a release tarball
# Prints each finding and exits 1 when there is one.
#
# Built-in checks (generic, safe to keep in the repository): access tokens and
# API keys, private keys and certificates, e-mail addresses, MAC addresses,
# IPv4 addresses, Steam user ids in paths, home directories other than the
# default /home/steamos. Placeholders used by the tests and docs are allowed:
# 127.x, 0.0.0.0, 192.0.2.x, 198.51.100.x, 203.0.113.x (RFC 5737), 192.168.1.x,
# 10.35.x.x, 100.64.0.x, 10.0.0.x, 10.1.1.x, 10.9.9.x, multicast, locally administered and all-zero MACs.
#
# Personal values (your own host names, addresses, ids, network names) must not
# be written into the repository, not even as search patterns. Put them, one
# extended regular expression per line, in a file outside it:
#   MLXR_PRIVACY_PATTERNS=~/.config/moonlight-xr-privacy-patterns.txt (the default)
# and they are searched for too, case-insensitively.
set -uo pipefail
cd "$(dirname "$0")/.."
PAT="${MLXR_PRIVACY_PATTERNS:-$HOME/.config/moonlight-xr-privacy-patterns.txt}"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
if [ $# -eq 0 ]; then
    git ls-files -z | grep -zv '^third_party/' > "$TMP/list"
    BASE=.
elif [ -f "$1" ]; then
    mkdir "$TMP/x" && tar -xf "$1" -C "$TMP/x" || { echo "cannot unpack $1"; exit 2; }
    BASE="$TMP/x"; ( cd "$BASE" && find . -type f -print0 ) > "$TMP/list"
else
    BASE="$1"; ( cd "$BASE" && find . -type f -not -path './.git/*' -print0 ) > "$TMP/list"
fi
python3 -I - "$BASE" "$TMP/list" "$PAT" <<'PY'
import ipaddress, os, re, sys
base, listp, patp = sys.argv[1:4]
files = [f for f in open(listp, "rb").read().split(b"\0") if f]
nets_ok = [ipaddress.IPv4Network(n) for n in ("127.0.0.0/8", "0.0.0.0/32", "192.0.2.0/24", "198.51.100.0/24",
           "203.0.113.0/24", "192.168.1.0/24", "10.35.0.0/16", "100.64.0.0/24", "10.0.0.0/24", "10.1.1.0/24", "10.9.9.0/24", "224.0.0.0/4", "255.255.255.255/32")]
ip_ok_text = {"8.8.8.8", "7.1.0.0", "3.23.0.74"}   # a public resolver (default route lookup); host version numbers in tests
generic = [
    ("token", re.compile(r"\b(ghp|gho|ghu|ghs|ghr)_[A-Za-z0-9]{20,}|\bgithub_pat_[A-Za-z0-9_]{20,}|\bsk-[A-Za-z0-9_-]{20,}|\bAKIA[0-9A-Z]{16}\b|\bxox[abps]-[A-Za-z0-9-]{10,}|\bAIza[0-9A-Za-z_-]{30,}")),
    ("key", re.compile(r"-----BEGIN (RSA |EC |OPENSSH |ENCRYPTED )?PRIVATE KEY-----\s*\n\s*[A-Za-z0-9+/]{20,}")),
    ("certificate", re.compile(r"-----BEGIN CERTIFICATE-----\s*\n\s*[A-Za-z0-9+/]{20,}")),
    ("email", re.compile(r"\b[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(\.[A-Za-z0-9-]+)*\.[A-Za-z]{2,}\b")),
    ("mac", re.compile(r"(?<![0-9A-Fa-f:])([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}(?![0-9A-Fa-f:])")),
    ("ipv4", re.compile(r"(?<![0-9A-Za-z.])\d{1,3}(\.\d{1,3}){3}(?![0-9A-Za-z]|\.\d)")),
    ("steam user id", re.compile(r"userdata/\d{5,}")),
    ("home directory", re.compile(r"/home/(?!steamos\b|runner\b|user\b|someone\b|<)[a-z_][a-z0-9_-]+/")),
]
email_ok = re.compile(r"@(example\.(com|org|net|invalid)|users\.noreply\.github\.com|anthropic\.com)$|^noreply@|"
                      r"@(kurims\.kyoto-u\.ac\.jp)$", re.I)   # licence texts of bundled code
def mac_ok(s):
    b = [int(x, 16) for x in s.split(":")]
    return b[0] & 2 or all(x == 0 for x in b) or s.lower() in ("aa:bb:cc:dd:ee:ff", "10:20:30:40:50:60") \
        or s.upper().startswith("AA:BB:CC:") or s.lower() == "1b:61:00:7f:ff:01"
def ip_ok(s, line):
    if s in ip_ok_text: return True
    try: a = ipaddress.IPv4Address(s)
    except ValueError: return True            # 7.1.431.0 and other version numbers
    if any(a in n for n in nets_ok): return True
    return bool(re.search(r"(?i)(version|ver|v)[\s=:\"'<>/A-Za-z]{0,14}" + re.escape(s), line))
personal = []
if os.path.isfile(patp):
    for l in open(patp, encoding="utf-8"):
        l = l.strip()
        if l and not l.startswith("#"): personal.append(re.compile(l, re.I))
print("privacy-check: %d files, %d personal patterns from %s" % (len(files), len(personal),
      "the local list" if personal else "nowhere (no local list; generic checks only)"))
found = 0
for f in files:
    p = os.path.join(base, f.decode("utf-8", "replace"))
    try: data = open(p, "rb").read()
    except OSError: continue
    if b"\0" in data[:4096] and not p.endswith((".ini", ".log", ".txt")):
        text = data.decode("latin-1")         # binaries: personal patterns and tokens only
        checks = [c for c in generic if c[0] in ("token",)]
    else:
        text = data.decode("utf-8", "replace"); checks = generic
    for n, line in enumerate(text.splitlines(), 1):
        hits = []
        for name, rx in checks:
            for m in rx.finditer(line):
                s = m.group(0)
                if name == "email" and email_ok.search(s): continue
                if name == "mac" and mac_ok(s): continue
                if name == "ipv4" and ip_ok(s, line): continue
                hits.append(name)
        hits += ["personal pattern %d" % (i + 1) for i, rx in enumerate(personal) if rx.search(line)]
        for h in hits:
            found += 1
            # never print the matched value itself: the output may be pasted somewhere
            print("  %s:%d: %s" % (os.path.relpath(p, base), n, h))
print("privacy-check: %d finding(s)" % found)
sys.exit(1 if found else 0)
PY
