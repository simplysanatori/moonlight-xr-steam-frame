#!/usr/bin/env python3
"""Write a synthetic Qt-style Moonlight.conf (QSettings INI dialect) for the seed test.
usage: make_qt_conf.py OUT client.pem client.key server.pem
The escaping below is written from Qt's documented behaviour (ini escaped string
writer), independently of the parser under test."""
import sys

out, cert_p, key_p, srv_p = sys.argv[1:5]
rd = lambda p: open(p, "rb").read()


def esc(data, bytearray_=True):
    """Escape like QSettings' iniEscapedString for a latin-1 byte string."""
    if bytearray_:
        data = b"@ByteArray(" + data + b")"
    s = data.decode("latin-1")
    res, need_q = [], False
    hexd = set("0123456789abcdefABCDEF")
    for i, ch in enumerate(s):
        o = ord(ch)
        if ch == "\n":
            res.append("\\n")
        elif ch == "\\":
            res.append("\\\\")
        elif ch == '"':
            res.append('\\"')
        elif o < 0x20 or o >= 0x7F:
            res.append("\\x%x" % o)
            if i + 1 < len(s) and s[i + 1] in hexd:
                res.append('""')   # keep the greedy \x escape from eating the next digit
        else:
            res.append(ch)
        if ch in ",;=" or ch == " ":
            need_q = True
    r = "".join(res)
    return '"%s"' % r if need_q or '"' in r.replace('\\"', "") else r


lines = ["[General]", "bitrate=31000", "certificate=" + esc(rd(cert_p)), "fps=144",
         "key=" + esc(rd(key_p)), "[hosts]"]
# host 1: paired, VPN-style addresses, loopback manual address, binary MAC with a \x1b + hex-digit trap
mac1 = bytes([0x02, 0xf0, 0x2c, 0x5d, 0xa5, 0x10])
lines += [
    "1\\customname=false",
    "1\\hostname=gaming-pc",
    "1\\ipv6address=",
    "1\\ipv6port=0",
    "1\\localaddress=100.64.0.5",
    "1\\localport=47989",
    "1\\mac=" + esc(mac1),
    "1\\manualaddress=127.0.0.1",
    "1\\manualport=47989",
    "1\\nvidiasw=false",
    "1\\remoteaddress=203.0.113.7",
    "1\\remoteport=47989",
    "1\\srvcert=" + esc(rd(srv_p)),
    "1\\uuid=6E1F0C2A-3B4D-4E5F-8A9B-0C1D2E3F4A5B",
    "1\\apps\\1\\id=881448767",
    "1\\apps\\1\\name=Desktop",
    "1\\apps\\2\\id=1439608285",
    "1\\apps\\2\\name=Assassin's Creed Origins",
]
# host 2: LAN host, never paired (no srvcert), MAC bytes 0x1b 0x61 .. to exercise the \x + hex digit guard
mac2 = bytes([0x1b, 0x61, 0x00, 0x7f, 0xff, 0x01])
lines += [
    "2\\hostname=laptop-é".encode("utf-8").decode("latin-1") if False else "2\\hostname=laptop",
    "2\\localaddress=192.168.1.77",
    "2\\localport=47990",
    "2\\mac=" + esc(mac2),
    "2\\manualaddress=",
    "2\\manualport=47989",
    "2\\remoteaddress=",
    "2\\remoteport=47989",
    "2\\uuid=LAPTOP-UUID-0002",
]
# host 3: no uuid -> skipped
lines += ["3\\hostname=ghost", "3\\localaddress=10.1.1.1", "size=3"]
open(out, "wb").write(("\n".join(lines) + "\n").encode("latin-1"))
