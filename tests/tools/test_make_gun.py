#!/usr/bin/env python3
"""Tests tools/xr/make_gun.py, which writes the Gun controller model (an original low-poly pistol, MXFM).
usage: test_make_gun.py MAKE_GUN.py [ASSETS_MODELS_DIR]

The generator is run into a temporary folder and its two files are read back with a Python mirror of
rooms/room_gpu.c (mlxr_model_file_parse and the range checks of openTextured: header 32 bytes, component
records of 208 bytes, vertices of 12 floats, uint32 indices, two RGBA8 images). Checked: parses; fewer
than 1500 triangles; two components, body and a `trigger` that rotates about x from /input/trigger with
its pivot under the bore; the size of a hand held pistol (0.18 to 0.22 m long, the muzzle at the minimum
z and on the bore axis, so the aim ray leaves it; the grip below the origin, raked back); unit normals
and tangent frames that agree with each triangle's winding (flat shading); no degenerate triangles;
every component a closed surface (each directed edge matched by its reverse) enclosing positive volume;
sights standing above the slide at one height, parallel to the bore; a palette image whose texels the
vertices' coordinates land on; a full trigger pull staying clear of the grip; the left file the right
one mirrored in x (positions, normals, winding, trigger pivot and axis); and, if the models folder is
given, the committed gun_right.model and gun_left.model being exactly what the generator writes."""
import math
import os
import struct
import subprocess
import sys
import tempfile

MAKE_GUN = sys.argv[1]
COMMITTED = sys.argv[2] if len(sys.argv) > 2 else None
failed = 0
checks = 0
VERTEX_FLOATS = 12
COMPONENT_BYTES = 208
GRIP_RAKE_DEG = 22.0   # the generator's own figure: the grip's front face leans this far


def check(cond, what):
    global failed, checks
    checks += 1
    if not cond:
        failed += 1
        print(f'FAIL: {what}')


def parse_model(path):
    """mlxr_model_file_parse's rules and openTextured's range checks; AssertionError if the client
    would refuse the file."""
    d = open(path, 'rb').read()
    assert d[:4] == b'MXFM', 'magic'
    version, vc, ic, cc, tw, th, flags = struct.unpack_from('<7I', d, 4)
    assert version in (1, 2) and flags == 0, 'version'
    assert 0 < vc < 1 << 22 and 0 < ic <= 1 << 24 and ic % 3 == 0 and 0 < cc <= 32, 'counts'
    assert 0 < tw <= 8192 and 0 < th <= 8192, 'texture size'
    voff = 32 + cc * COMPONENT_BYTES
    ioff = voff + vc * VERTEX_FLOATS * 4
    toff = ioff + ic * 4
    assert len(d) == toff + 2 * tw * th * 4, 'size adds up'
    comps = []
    for c in range(cc):
        rec = 32 + c * COMPONENT_BYTES
        name = d[rec:rec + 32].split(b'\0')[0].decode()
        path_in = d[rec + 32:rec + 64].split(b'\0')[0].decode()
        first, count, mtype, zero = struct.unpack_from('<4I', d, rec + 64)
        motion = struct.unpack_from('<32f', d, rec + 80)
        assert first + count <= ic and first % 3 == 0 and count % 3 == 0, f'component {name} range'
        assert mtype <= (4 if version >= 2 else 3) and zero == 0, 'motion type'
        comps.append({'name': name, 'path': path_in, 'first': first, 'count': count, 'type': mtype,
                      'motion': motion})
    floats = struct.unpack_from(f'<{vc * VERTEX_FLOATS}f', d, voff)
    verts = [floats[i * VERTEX_FLOATS:(i + 1) * VERTEX_FLOATS] for i in range(vc)]
    idx = struct.unpack_from(f'<{ic}I', d, ioff)
    assert all(i < vc for i in idx), 'index range'
    return {'version': version, 'vc': vc, 'ic': ic, 'comps': comps, 'verts': verts, 'idx': idx,
            'tw': tw, 'th': th, 'colour': d[toff:toff + tw * th * 4], 'bytes': d}


def sub(a, b): return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def cross(a, b): return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
def length(a): return math.sqrt(dot(a, a))


def triangles(m, comp=None):
    """Corners ((position, normal) x 3) of a component's triangles, or of all of them."""
    lo, hi = (0, m['ic']) if comp is None else (comp['first'], comp['first'] + comp['count'])
    for t in range(lo, hi, 3):
        yield [(m['verts'][i][0:3], m['verts'][i][3:6]) for i in m['idx'][t:t + 3]]


def bounds(points):
    return [min(p[k] for p in points) for k in range(3)], [max(p[k] for p in points) for k in range(3)]


def key(p):
    return tuple(round(c * 1e6) for c in p)


def closed_with_volume(m, comp):
    """(every directed edge has its reverse, the enclosed volume by the divergence theorem)."""
    edges = {}
    volume = 0.0
    for tri in triangles(m, comp):
        pts = [p for p, _ in tri]
        volume += dot(pts[0], cross(pts[1], pts[2])) / 6.0
        for a, b in ((0, 1), (1, 2), (2, 0)):
            e = (key(pts[a]), key(pts[b]))
            edges[e] = edges.get(e, 0) + 1
    return all(edges.get((b, a), 0) == n for (a, b), n in edges.items()), volume


def close(a, b, eps=1e-6):
    return len(a) == len(b) and all(abs(x - y) <= eps for x, y in zip(a, b))


def run_generator(args):
    rc = subprocess.run([sys.executable, '-I', MAKE_GUN] + args, capture_output=True, text=True)
    check(rc.returncode == 0, f'make_gun.py {" ".join(args)} runs ({rc.stderr.strip()[:300]})')
    return rc.returncode == 0


def examine(m, name):
    """Checks one hand's file; returns its trigger pivot and axis."""
    check(m['version'] == 2, f'{name}: MXFM version 2')
    check(100 < m['ic'] // 3 < 1500, f'{name}: more than a handful and under 1500 triangles ({m["ic"] // 3})')
    check([c['name'] for c in m['comps']] == ['body', 'trigger'], f'{name}: components body, trigger')
    body, trig = m['comps']
    check(body['type'] == 0 and body['path'] == '', f'{name}: the body does not move')
    check(trig['path'] == '/input/trigger' and trig['type'] == 2,
          f'{name}: the trigger rotates, driven by /input/trigger')
    check(body['first'] + body['count'] == trig['first'] and trig['first'] + trig['count'] == m['ic'],
          f'{name}: the two components cover every index')
    mo = trig['motion']
    pivot, axis, rng = mo[0:3], mo[3:6], mo[6:8]
    check(abs(pivot[0]) < 1e-6 and -0.03 < pivot[1] < -0.005 and -0.05 < pivot[2] < -0.01,
          f'{name}: trigger pivot is on the centre plane, under the bore, in front of the grip ({pivot})')
    check(abs(abs(axis[0]) - 1) < 1e-5, f'{name}: trigger axis is x ({axis})')
    check(rng[0] == 0 and -45 < rng[1] < -10, f'{name}: the range turns the foot back, in degrees ({rng})')

    pts = [v[0:3] for v in m['verts']]
    lo, hi = bounds(pts)
    check(0.18 <= hi[2] - lo[2] <= 0.22, f'{name}: 0.18 to 0.22 m long ({hi[2] - lo[2]:.4f})')
    check(0.10 <= hi[1] - lo[1] <= 0.15 and 0.02 <= hi[0] - lo[0] <= 0.04,
          f'{name}: height and width of a pistol ({hi[1] - lo[1]:.3f} x {hi[0] - lo[0]:.3f})')
    check(lo[2] < -0.10 and hi[2] > 0.03, f'{name}: the barrel in front of the origin, the slide behind it')
    muzzle = [p for p in pts if abs(p[2] - lo[2]) < 1e-6]
    check(muzzle and all(math.hypot(p[0], p[1]) < 0.0085 for p in muzzle),
          f'{name}: the front-most points are the muzzle, round the bore axis')
    cx, cy = sum(p[0] for p in muzzle) / len(muzzle), sum(p[1] for p in muzzle) / len(muzzle)
    check(abs(cx) < 1e-5 and abs(cy) < 1e-5, f'{name}: the muzzle is centred on the aim ray ({cx}, {cy})')
    check(lo[1] < -0.08, f'{name}: the grip hangs below the origin ({lo[1]:.3f})')
    check(abs(lo[0] + hi[0]) < 0.004, f'{name}: centred left to right ({lo[0]:.4f} {hi[0]:.4f})')
    # The grip leans back at the bottom: its lowest points are behind its highest
    low = [p for p in pts if p[1] < lo[1] + 0.004]
    top_grip = [p for p in pts if -0.0105 < p[1] < -0.0095 and p[2] > 0.0]
    shift = sum(p[2] for p in low) / len(low) - sum(p[2] for p in top_grip) / len(top_grip)
    check(0.02 < shift < 0.06, f'{name}: the grip is raked back ({shift:.4f} m over its length)')

    flat = True
    for tri in triangles(m):
        a, b, c = (p for p, _ in tri)
        n = cross(sub(b, a), sub(c, a))
        area = length(n) / 2
        check(area > 1e-8, f'{name}: no degenerate triangle ({a} {b} {c})')
        if area > 1e-8:
            n = [x / (2 * area) for x in n]
            flat = flat and all(abs(length(vn) - 1) < 1e-4 and dot(vn, n) > 0.9999 for _, vn in tri)
    check(flat, f'{name}: flat, unit normals that agree with every triangle\'s winding')
    check(all(abs(length(v[6:9]) - 1) < 1e-4 and abs(dot(v[3:6], v[6:9])) < 1e-4 and abs(abs(v[9]) - 1) < 1e-6
              for v in m['verts']), f'{name}: unit tangents, square to the normals')

    for comp in m['comps']:
        balanced, volume = closed_with_volume(m, comp)
        check(balanced, f'{name}: {comp["name"]} is a closed surface (no holes, consistent winding)')
        check(volume > 1e-8, f'{name}: {comp["name"]} faces outward, enclosing {volume:.3e} m3')

    # Sights: posts above the slide top, ending at one height; a post at the front, a notch behind
    sight = [p for p in pts if p[1] > 0.0205]
    check(len(sight) > 0, f'{name}: sights stand above the slide')
    if sight:
        top = max(p[1] for p in sight)
        ends = {round(p[0], 6) for p in sight if abs(p[1] - top) < 1e-6}
        check(0.022 < top < 0.035, f'{name}: sight height ({top:.4f})')
        check(min(p[2] for p in sight) < -0.08 and max(p[2] for p in sight) > 0.02,
              f'{name}: a front sight near the muzzle and a rear sight near the back')
        rear = [p for p in sight if p[2] > 0.0]
        front = [p for p in sight if p[2] < -0.05]
        check(all(abs(p[0]) > 0.003 for p in rear) and max(abs(p[0]) for p in rear) > 0.008,
              f'{name}: the rear sight is a notch with a gap on the centre line')
        check(max(abs(p[0]) for p in front) < 0.004 and all(abs(p[1] - top) < 1e-6 or p[1] < top for p in front),
              f'{name}: the front sight is a post on the centre line')
        check(all(abs(p[1] - top) < 1e-6 for p in sight if p[1] > top - 0.002), f'{name}: one sight height ({ends})')

    # The palette: one row of texels, every vertex on the middle of one
    tw, th = m['tw'], m['th']
    check(th == 1 and 3 <= tw <= 8, f'{name}: a small palette image ({tw}x{th})')
    used = set()
    for v in m['verts']:
        texel = v[10] * tw - 0.5
        check(abs(texel - round(texel)) < 1e-4 and abs(v[11] - 0.5) < 1e-6 and 0 <= round(texel) < tw,
              f'{name}: texture coordinate ({v[10]}, {v[11]}) is on a palette texel')
        used.add(round(texel))
    check(len(used) >= 3, f'{name}: at least three flat materials are used ({len(used)})')
    texels = [tuple(m['colour'][4 * i:4 * i + 3]) for i in range(tw)]
    check(len(set(texels)) == tw and all(m['colour'][4 * i + 3] == 255 for i in range(tw)),
          f'{name}: distinct palette colours, no ambient occlusion')

    # A full pull swings the foot back toward the palm, and the foot stays in front of the grip face
    ang = math.radians(rng[1])
    foot = min((p for tri in triangles(m, trig) for p, _ in tri), key=lambda p: p[1])
    dy, dz = foot[1] - pivot[1], foot[2] - pivot[2]
    pulled_z = pivot[2] + dy * math.sin(ang) + dz * math.cos(ang)
    pulled_y = pivot[1] + dy * math.cos(ang) - dz * math.sin(ang)
    grip_front = 0.010 + (-0.010 - pulled_y) * math.tan(math.radians(GRIP_RAKE_DEG))
    check(pulled_z > foot[2] + 0.005 and pulled_z < grip_front - 0.005,
          f'{name}: pulled, the foot ({pulled_z:.4f}) moves back and clears the grip face ({grip_front:.4f})')
    return pivot, axis


def main():
    with tempfile.TemporaryDirectory() as work:
        if not run_generator([work]):
            return 1
        rpath, lpath = os.path.join(work, 'gun_right.model'), os.path.join(work, 'gun_left.model')
        try:
            right, left = parse_model(rpath), parse_model(lpath)
        except AssertionError as e:
            print(f'FAIL: the client would refuse the model: {e}')
            return 1
        r_pivot, r_axis = examine(right, 'right')
        l_pivot, l_axis = examine(left, 'left')

        rlo, rhi = bounds([v[0:3] for v in right['verts']])
        llo, lhi = bounds([v[0:3] for v in left['verts']])
        check(close([-rhi[0], rlo[1], rlo[2]], llo) and close([-rlo[0], rhi[1], rhi[2]], lhi),
              'the left bounds are the right bounds mirrored in x')
        check(right['ic'] == left['ic'] and right['vc'] == left['vc'], 'both hands have the same counts')
        check(sorted((key((-v[0], v[1], v[2])), key((-v[3], v[4], v[5]))) for v in right['verts']) ==
              sorted((key(v[0:3]), key(v[3:6])) for v in left['verts']),
              'every left vertex and normal is a right one mirrored in x')
        # The winding is flipped with it: each triangle, mirrored, is a left one with its corners reversed
        def corner_sets(m, mirror):
            out = []
            for tri in triangles(m):
                pts = [(-p[0] if mirror else p[0], p[1], p[2]) for p, _ in tri]
                if mirror:
                    pts = [pts[0], pts[2], pts[1]]
                k = [key(p) for p in pts]
                i = k.index(min(k))
                out.append(tuple(k[i:] + k[:i]))
            return sorted(out)
        check(corner_sets(right, True) == corner_sets(left, False),
              'every left triangle is a right one mirrored with its winding flipped')
        check(close(l_pivot, [-r_pivot[0], r_pivot[1], r_pivot[2]]),
              f'the left trigger pivot is the right one mirrored ({l_pivot})')
        check(abs(abs(l_axis[0]) - 1) < 1e-5 and l_axis[0] * r_axis[0] > 0, f'the left trigger axis is still x ({l_axis})')
        check(right['bytes'] != left['bytes'], 'the two files differ')

        # --out and --mirror give the same files as the folder form
        single = os.path.join(work, 'single.model')
        if run_generator(['--out', single, '--mirror']):
            check(open(single, 'rb').read() == open(lpath, 'rb').read(), '--out FILE --mirror writes the left file')
        again = os.path.join(work, 'again.model')
        if run_generator(['--out', again]):
            check(open(again, 'rb').read() == open(rpath, 'rb').read(), '--out FILE writes the right file')
        rc = subprocess.run([sys.executable, '-I', MAKE_GUN, '--bogus'], capture_output=True, text=True)
        check(rc.returncode != 0 and 'usage' in rc.stderr + rc.stdout, 'bad arguments print the usage')

        if COMMITTED:
            for name, fresh in (('gun_right.model', rpath), ('gun_left.model', lpath)):
                path = os.path.join(COMMITTED, name)
                check(os.path.isfile(path) and open(path, 'rb').read() == open(fresh, 'rb').read(),
                      f'{path} is what tools/xr/make_gun.py writes (python3 tools/xr/make_gun.py assets/models)')

    print(f'{checks - failed}/{checks} checks passed')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
