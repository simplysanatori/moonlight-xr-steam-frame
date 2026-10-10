#!/usr/bin/env python3
"""pngdiff.py A.png B.png [max-differing-pixels]: number of differing pixels between two 8-bit PNGs (stdlib only). GPLv3."""
import struct, sys, zlib

def load(p):
    d = open(p, 'rb').read()
    assert d[:8] == b'\x89PNG\r\n\x1a\n'
    i, idat, w = 8, b'', None
    while i < len(d):
        n, t = struct.unpack('>I4s', d[i:i+8])
        body = d[i+8:i+8+n]
        if t == b'IHDR':
            w, h, bd, ct = struct.unpack('>IIBB', body[:10]); assert bd == 8 and ct in (2, 6)
        elif t == b'IDAT':
            idat += body
        i += 12 + n
    bpp = 4 if ct == 6 else 3
    raw = zlib.decompress(idat); stride = w * bpp
    out, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y*(stride+1)]; line = bytearray(raw[y*(stride+1)+1:(y+1)*(stride+1)])
        for x in range(stride):
            a = line[x-bpp] if x >= bpp else 0; b = prev[x]; c = prev[x-bpp] if x >= bpp else 0
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2*c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        out.append(bytes(line)); prev = line
    return w, h, bpp, out

limit = int(sys.argv[3]) if len(sys.argv) > 3 else 0   # tolerated differing pixels (a spinner's phase)
wa, ha, ba, A = load(sys.argv[1]); wb, hb, bb, B = load(sys.argv[2])
if (wa, ha) != (wb, hb):
    print("size differs: %dx%d vs %dx%d" % (wa, ha, wb, hb)); sys.exit(2)
diff = 0
for ra, rb in zip(A, B):
    if ra != rb:
        for x in range(wa):
            if ra[x*ba:x*ba+3] != rb[x*bb:x*bb+3]: diff += 1
print("%d of %d pixels differ" % (diff, wa*ha))
sys.exit(0 if diff <= limit else 1)
