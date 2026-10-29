#!/usr/bin/env python3
"""snapshot_golden.py SNAPDIR GOLDENDIR: the in-headset UI's snapshots
(mlxr-ui-snapshot, test m4_snapshot_budget) against the committed goldens.
Stdlib only. GPLv3.

Each snapshot (1600x1200) is shrunk 4x by a box average to 400x300 (the
goldens are kept small; a moved panel, a lost row or a wrong colour still
shows at that size) and compared with GOLDENDIR/<name>.png:
  a pixel differs when any channel is off by more than TOL (16 of 255), and
  a state fails when more than MAX_FRAC (0.1%, 120 pixels) of them differ,
so rasteriser drift (another compiler's float rounding) passes and a moved,
added or lost panel, row or button fails. A changed word in small text may
not: that is what the full size snapshots are for. (Two CI runs gave
bit-identical snapshots.)
A golden with no snapshot fails; a snapshot with no golden is reported only.
The shrunk snapshots land in SNAPDIR/golden-actual/ either way (the CI
artefact carries them), ready to be copied over the goldens.

MLXR_UPDATE_GOLDEN=1 writes the shrunk snapshots into GOLDENDIR and passes:
    MLXR_UPDATE_GOLDEN=1 ctest --test-dir build -R m4_snapshot
or, from a CI artefact, python3 snapshot_golden.py <artefact>/snapshots tests/ui/golden
with the variable set. Exits 77 (skipped) when GOLDENDIR holds no goldens.
"""
import os
import struct
import sys
import zlib

SCALE = 4
TOL = 16
MAX_FRAC = 0.001


def load(path):
    """(w, h, rows): rows of 8-bit RGB bytes (alpha dropped)."""
    d = open(path, 'rb').read()
    if d[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('%s: not a PNG' % path)
    i, idat, w = 8, [], None
    while i < len(d):
        n, t = struct.unpack('>I4s', d[i:i + 8])
        body = d[i + 8:i + 8 + n]
        if t == b'IHDR':
            w, h, bd, ct, _, _, il = struct.unpack('>IIBBBBB', body[:13])
            if bd != 8 or ct not in (2, 6) or il != 0:
                raise ValueError('%s: only 8-bit RGB/RGBA, not interlaced' % path)
        elif t == b'IDAT':
            idat.append(body)
        elif t == b'IEND':
            break
        i += 12 + n
    bpp = 4 if ct == 6 else 3
    raw = zlib.decompress(b''.join(idat))
    stride = w * bpp
    rows, prev = [], bytearray(stride)
    for y in range(h):
        o = y * (stride + 1)
        f = raw[o]
        line = bytearray(raw[o + 1:o + 1 + stride])
        if f == 1:
            for x in range(bpp, stride):
                line[x] = (line[x] + line[x - bpp]) & 255
        elif f == 2:
            line = bytearray((a + b) & 255 for a, b in zip(line, prev))
        elif f == 3:
            for x in range(stride):
                a = line[x - bpp] if x >= bpp else 0
                line[x] = (line[x] + ((a + prev[x]) >> 1)) & 255
        elif f == 4:
            for x in range(stride):
                if x >= bpp:
                    a, c = line[x - bpp], prev[x - bpp]
                else:
                    a = c = 0
                b = prev[x]
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        elif f != 0:
            raise ValueError('%s: bad filter %d' % (path, f))
        rows.append(line)
        prev = line
    if bpp == 4:
        rows = [bytearray(b for k, b in enumerate(r) if k % 4 != 3) for r in rows]
    return w, h, rows


def shrink(w, h, rows):
    """Box average over SCALE x SCALE blocks; (w, h, rows) of the result."""
    sw, sh = w // SCALE, h // SCALE
    out = []
    n = SCALE * SCALE
    for y in range(sh):
        acc = [0] * (sw * 3)
        for r in rows[y * SCALE:(y + 1) * SCALE]:
            for c in range(3):
                ch = r[c::3]
                for x in range(sw):
                    acc[x * 3 + c] += sum(ch[x * SCALE:(x + 1) * SCALE])
        out.append(bytes((v + n // 2) // n for v in acc))
    return sw, sh, out


def save(path, w, h, rows):
    # each row filtered None, Sub or Up, whichever sums smallest (as libpng
    # guesses), to keep the committed goldens small
    lines, prev = [], bytes(w * 3)
    for r in rows:
        r = bytes(r)
        sub = bytes((r[x] - (r[x - 3] if x >= 3 else 0)) & 255 for x in range(len(r)))
        up = bytes((a - b) & 255 for a, b in zip(r, prev))
        cost = lambda f: sum(v if v < 128 else 256 - v for v in f)
        best = min(((cost(r), 0, r), (cost(sub), 1, sub), (cost(up), 2, up)), key=lambda c: c[0])
        lines.append(bytes([best[1]]) + best[2])
        prev = r
    raw = b''.join(lines)

    def chunk(t, body):
        return struct.pack('>I', len(body)) + t + body + struct.pack('>I', zlib.crc32(t + body) & 0xffffffff)

    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')
    with open(path, 'wb') as f:
        f.write(png)


def differing(a, b, w):
    n = 0
    for ra, rb in zip(a, b):
        if ra == rb:
            continue
        for x in range(w):
            k = x * 3
            if (abs(ra[k] - rb[k]) > TOL or abs(ra[k + 1] - rb[k + 1]) > TOL
                    or abs(ra[k + 2] - rb[k + 2]) > TOL):
                n += 1
    return n


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    snaps, golden = argv[1], argv[2]
    update = os.environ.get('MLXR_UPDATE_GOLDEN', '') not in ('', '0')
    names = sorted(f[:-4] for f in os.listdir(snaps) if f.endswith('.png'))
    if not names:
        print('no snapshots in %s' % snaps)
        return 1
    actual = os.path.join(snaps, 'golden-actual')
    os.makedirs(actual, exist_ok=True)
    small = {}
    for name in names:
        small[name] = shrink(*load(os.path.join(snaps, name + '.png')))
        save(os.path.join(actual, name + '.png'), *small[name])
    if update:
        os.makedirs(golden, exist_ok=True)
        for name in names:
            save(os.path.join(golden, name + '.png'), *small[name])
        print('MLXR_UPDATE_GOLDEN=1: wrote %d goldens to %s' % (len(names), golden))
        return 0
    goldens = sorted(f[:-4] for f in os.listdir(golden) if f.endswith('.png')) if os.path.isdir(golden) else []
    if not goldens:
        print('no goldens in %s: skipped (MLXR_UPDATE_GOLDEN=1 makes them)' % golden)
        return 77
    bad = 0
    for name in goldens:
        if name not in small:
            print('%-22s FAIL: golden with no snapshot' % name)
            bad += 1
            continue
        sw, sh, srows = small[name]
        gw, gh, grows = load(os.path.join(golden, name + '.png'))
        if (sw, sh) != (gw, gh):
            print('%-22s FAIL: %dx%d, golden %dx%d' % (name, sw, sh, gw, gh))
            bad += 1
            continue
        n = differing(srows, grows, sw)
        limit = int(MAX_FRAC * sw * sh)
        ok = n <= limit
        print('%-22s %6d of %d pixels differ (limit %d) %s' % (name, n, sw * sh, limit, 'ok' if ok else 'FAIL'))
        bad += not ok
    for name in names:
        if name not in goldens:
            print('%-22s no golden (new state? MLXR_UPDATE_GOLDEN=1 adds it)' % name)
    if bad:
        print('%d state(s) differ from the goldens; the shrunk snapshots are in %s' % (bad, actual))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
