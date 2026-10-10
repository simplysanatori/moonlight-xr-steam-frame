#!/usr/bin/env python3
"""Import the client identity and paired hosts of Moonlight Qt into ~/.config/moonlight-xr.

Read-only on the Qt side. Parses Qt's QSettings INI dialect (quoted strings,
C escapes, @ByteArray(...)) with the standard library only. Never prints the
private key or full certificates: only SHA-256 fingerprints and lengths.
Run as:  python3 -I seed_moonlight_qt.py [--conf FILE] [--dest DIR] [--force]
"""
import argparse
import base64
import hashlib
import os
import re
import sys
import tempfile

FLATPAK_CONF = "~/.var/app/com.moonlight_stream.Moonlight/config/Moonlight Game Streaming Project/Moonlight.conf"
NATIVE_CONF = "~/.config/Moonlight Game Streaming Project/Moonlight.conf"

_SIMPLE = {"a": 7, "b": 8, "f": 12, "n": 10, "r": 13, "t": 9, "v": 11, "'": 39, '"': 34, "?": 63, "\\": 92}
_HEX = set("0123456789abcdefABCDEF")


def qt_unescape(raw):
    """Qt's iniUnescapedStringImpl: quotes toggle (and vanish), C escapes, \\xH+ is greedy."""
    out = []
    i, n = 0, len(raw)
    while i < n:
        c = raw[i]
        if c == '"':
            i += 1
        elif c == "\\":
            i += 1
            if i >= n:
                break
            e = raw[i]
            i += 1
            if e in _SIMPLE:
                out.append(chr(_SIMPLE[e]))
            elif e == "x":
                j = i
                while j < n and raw[j] in _HEX:
                    j += 1
                out.append(chr(int(raw[i:j] or "0", 16) & 0xFFFF))
                i = j
            elif e in "01234567":
                j = i
                while j < n and j < i + 2 and raw[j] in "01234567":
                    j += 1
                out.append(chr(int(raw[i - 1:j], 8) & 0xFFFF))
                i = j
            else:
                out.append(e)
        else:
            out.append(c)
            i += 1
    return "".join(out)


def decode_value(raw):
    """Return bytes for @ByteArray(...), else str."""
    s = qt_unescape(raw.strip())
    if s.startswith("@ByteArray(") and s.endswith(")"):
        inner = s[len("@ByteArray("):-1]
        try:
            return inner.encode("latin-1")
        except UnicodeEncodeError:
            return inner.encode("utf-8", "surrogateescape")
    return s


def parse_ini(text):
    sections, cur = {}, None
    for line in text.splitlines():
        line = line.strip("\r\n")
        if not line.strip() or line.lstrip().startswith((";", "#")):
            continue
        m = re.match(r"^\[(.*)\]\s*$", line)
        if m:
            cur = sections.setdefault(m.group(1), {})
            continue
        if cur is None or "=" not in line:
            continue
        k, v = line.split("=", 1)
        cur[k.strip()] = v
    return sections


def as_text(v):
    if isinstance(v, bytes):
        return v.decode("utf-8", "replace")
    return v or ""


def pem_fingerprint(pem):
    m = re.search(rb"-----BEGIN CERTIFICATE-----(.*?)-----END CERTIFICATE-----", pem, re.S)
    if not m:
        return None
    try:
        return hashlib.sha256(base64.b64decode(b"".join(m.group(1).split()))).hexdigest()
    except Exception:
        return None


def looks_like_key(b):
    return bool(re.search(rb"-----BEGIN (RSA |EC )?PRIVATE KEY-----", b)) and b"-----END" in b


def atomic_write(path, data, mode):
    d = os.path.dirname(path)
    fd, tmp = tempfile.mkstemp(dir=d, prefix=".seed.")
    try:
        os.fchmod(fd, mode)
        with os.fdopen(fd, "wb") as f:
            f.write(data)
        os.replace(tmp, path)
    except BaseException:
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise


def is_loopback(a):
    return a.startswith("127.") or a in ("::1", "localhost")


def read_hosts_ini(path):
    hosts, cur = [], None
    if not os.path.exists(path):
        return hosts
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line or line[0] in "#;":
                continue
            if line.startswith("["):
                cur = {"uuid": line[1:line.index("]")] if "]" in line else line[1:]}
                hosts.append(cur)
            elif "=" in line and cur is not None:
                k, v = line.split("=", 1)
                cur[k.strip()] = v.strip()
    return hosts


def one_line(s):
    return str(s).replace("\n", " ").replace("\r", " ")


def write_hosts_ini(path, hosts):
    lines = ["# Moonlight XR hosts. Pinned server certificates live in servercerts/."]
    for h in hosts:
        lines.append("")
        lines.append("[%s]" % one_line(h["uuid"]).replace("]", "_"))
        for k in ("name", "address", "extra_addresses", "mac", "http_port", "https_port", "cert"):
            if h.get(k) not in (None, ""):
                lines.append("%s = %s" % (k, one_line(h[k])))
    atomic_write(path, ("\n".join(lines) + "\n").encode("utf-8"), 0o600)


def safe_name(uuid):
    return re.sub(r"[^A-Za-z0-9_-]", "_", uuid or "host")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--conf", help="Moonlight.conf to read (default: flatpak, then native location)")
    ap.add_argument("--dest", help="destination dir (default: $MLXR_CONFIG_DIR or ~/.config/moonlight-xr)")
    ap.add_argument("--force", action="store_true", help="overwrite an existing client identity")
    args = ap.parse_args()

    conf = args.conf
    if not conf:
        for c in (FLATPAK_CONF, NATIVE_CONF):
            if os.path.isfile(os.path.expanduser(c)):
                conf = os.path.expanduser(c)
                break
    if not conf or not os.path.isfile(conf):
        print("no Moonlight Qt config found (looked at the flatpak and native locations); nothing to import")
        return 3
    dest = args.dest or os.environ.get("MLXR_CONFIG_DIR") or os.path.expanduser("~/.config/moonlight-xr")
    os.makedirs(dest, mode=0o700, exist_ok=True)
    os.chmod(dest, 0o700)

    with open(conf, "rb") as f:    # read-only
        text = f.read().decode("utf-8", "surrogateescape")
    ini = parse_ini(text)
    gen = ini.get("General", {})
    print("source: %s (%d bytes)" % (conf, len(text)))

    # ---- identity ----
    cert = decode_value(gen["certificate"]) if "certificate" in gen else b""
    key = decode_value(gen["key"]) if "key" in gen else b""
    rc = 0
    if isinstance(cert, bytes) and pem_fingerprint(cert) and isinstance(key, bytes) and looks_like_key(key):
        pc, pk = os.path.join(dest, "client.pem"), os.path.join(dest, "client.key")
        if (os.path.exists(pc) or os.path.exists(pk)) and not args.force:
            print("identity: destination already has client.pem/client.key, kept (use --force to replace)")
        else:
            atomic_write(pk, key if key.endswith(b"\n") else key + b"\n", 0o600)
            atomic_write(pc, cert if cert.endswith(b"\n") else cert + b"\n", 0o644)
            pu = os.path.join(dest, "uniqueid")
            uid = decode_value(gen["uniqueid"]) if "uniqueid" in gen else ""
            uid = as_text(uid).strip()
            if re.fullmatch(r"[0-9a-fA-F]{1,16}", uid or ""):
                atomic_write(pu, (uid.lower().rjust(16, "0") + "\n").encode(), 0o600)
                print("identity: unique id imported")
            elif os.path.exists(pu) and args.force:
                os.unlink(pu)   # new keypair -> a fresh unique id is generated on first use
                print("identity: Moonlight stored no unique id; a new one is created on first use")
            else:
                print("identity: Moonlight stored no unique id; one is created on first use")
            print("identity: imported client.pem (sha256 %s) and client.key (%d bytes, mode 0600)"
                  % (pem_fingerprint(cert), len(key)))
    else:
        print("identity: certificate/key not found or unreadable in the [General] section")
        rc = 4

    # ---- hosts ----
    hs = ini.get("hosts", {})
    idxs = sorted({int(m.group(1)) for k in hs for m in [re.match(r"^(\d+)\\", k)] if m})
    ini_path = os.path.join(dest, "hosts.ini")
    existing = read_hosts_ini(ini_path)
    imported = 0
    for i in idxs:
        def g(name, default=""):
            v = hs.get("%d\\%s" % (i, name))
            return decode_value(v) if v is not None else default
        uuid = as_text(g("uuid")).strip()
        name = as_text(g("hostname")).strip() or "UNKNOWN"
        if not uuid:
            print("host #%d (%s): no uuid, skipped" % (i, name))
            continue
        mac = g("mac")
        mac_s = ":".join("%02x" % b for b in mac) if isinstance(mac, bytes) and len(mac) == 6 and any(mac) else ""
        addrs = []   # (address, port) in preference order
        for a, p in (("manualaddress", "manualport"), ("localaddress", "localport"),
                     ("remoteaddress", "remoteport"), ("ipv6address", "ipv6port")):
            addr = as_text(g(a)).strip()
            try:
                port = int(as_text(g(p, "47989")) or 47989)
            except ValueError:
                port = 47989
            if addr and not is_loopback(addr) and addr not in [x[0] for x in addrs]:
                addrs.append((addr, port))
        srvcert = g("srvcert")
        fp = pem_fingerprint(srvcert) if isinstance(srvcert, bytes) else None
        rec = {"uuid": uuid, "name": name, "mac": mac_s}
        if addrs:
            rec["address"], rec["http_port"] = addrs[0]
            rec["extra_addresses"] = ",".join(a for a, _ in addrs[1:])
        else:
            rec["address"], rec["http_port"] = "", 47989
        rec["https_port"] = 47984   # Qt does not persist it; serverinfo reports the real one
        if fp:
            os.makedirs(os.path.join(dest, "servercerts"), mode=0o700, exist_ok=True)
            rel = "servercerts/%s.pem" % safe_name(uuid)
            atomic_write(os.path.join(dest, rel), srvcert if srvcert.endswith(b"\n") else srvcert + b"\n", 0o644)
            rec["cert"] = rel
        for j, e in enumerate(existing):
            if e["uuid"].lower() == uuid.lower():
                existing[j] = rec
                break
        else:
            existing.append(rec)
        imported += 1
        napps = len({m.group(1) for k in hs for m in [re.match(r"^%d\\apps\\(\d+)\\" % i, k)] if m})
        print("host #%d: %s uuid=%s address=%s extra=%s mac=%s paired(cert)=%s%s apps=%d"
              % (i, name, uuid, rec["address"] or "-", rec.get("extra_addresses") or "-", mac_s or "-",
                 "yes" if fp else "NO", " sha256=" + fp[:16] + "..." if fp else "", napps))
    if imported:
        write_hosts_ini(ini_path, existing)
    print("hosts: %d imported into %s" % (imported, ini_path))
    return rc


if __name__ == "__main__":
    sys.exit(main())
