#!/usr/bin/env python3
"""Tests tools/xr/make_gun.py, which converts the Gun controller model (a G17 with a weapon light, CC BY
4.0) into the client's MXFM files, and the committed assets/models/gun_{right,left}.model.
usage: test_make_gun.py MAKE_GUN.py [ASSETS_MODELS_DIR]

A stand-in pistol made of boxes is written as a GLB shaped like a Sketchfab export (a Z-up root node
with a scale, a textured slide, flat colours, the trigger a loose piece inside the frame's mesh, a
spare magazine node and a loose part behind the pistol), converted with a profile of its own, and both
files read back with a Python mirror of rooms/room_gpu.c (mlxr_model_file_parse and the range checks
of openTextured). Checked: what is not the pistol is left out (by node name and by keep_box); length
scaled; the bore on the z axis, so the front face of the barrel is centred on the aim ray; the grip
placed on the pistol grip line; the trigger split out as its own component, rotating about x from
/input/trigger, hinged at its top front; colours from the texture and the materials on palette
texels; the left file the right one mirrored (positions, normals, winding, pivot); a missing trigger
or an empty selection refused. With numpy and pyfqmr, the over-budget path (simplify, recolour,
smooth normals). Then, if the models folder is given, the committed pair: what the client reads,
at most 30000 triangles a hand, body and trigger, the size of a G17, the slide's front on the aim
ray, the grip below the bore, the trigger hinged under the bore, and the left file the right mirrored."""
import json
import math
import os
import struct
import subprocess
import sys
import tempfile
import zlib

MAKE_GUN = sys.argv[1]
COMMITTED = sys.argv[2] if len(sys.argv) > 2 else None
failed = 0
checks = 0
VERTEX_FLOATS = 12
COMPONENT_BYTES = 208
GRIP_RAKE_DEG = 22.0
PISTOL_GRIP = (-0.0525, 0.043)


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
    lo, hi = (0, m['ic']) if comp is None else (comp['first'], comp['first'] + comp['count'])
    for t in range(lo, hi, 3):
        yield [(m['verts'][i][0:3], m['verts'][i][3:6]) for i in m['idx'][t:t + 3]]


def bounds(points):
    return [min(p[k] for p in points) for k in range(3)], [max(p[k] for p in points) for k in range(3)]


def key(p):
    return tuple(round(c * 1e6) for c in p)


def close(a, b, eps=1e-5):
    return len(a) == len(b) and all(abs(x - y) <= eps for x, y in zip(a, b))


# --- the stand-in -----------------------------------------------------------------------------------
#
# Authored Z-up in centimetres under a root node that turns it Y-up and scales it to metres, like
# Sketchfab's exports. In the Y-up world (metres) the barrel points along +x.

def outward(tris):
    vol = sum(dot(a, cross(b, c)) for a, b, c in tris)
    return tris if vol > 0 else [(a, c, b) for a, b, c in tris]


def prism(front, back):
    """A closed solid between two quads (4 corners each, same order), 12 triangles wound outward."""
    q = [(front[0], front[1], front[2], front[3]), (back[3], back[2], back[1], back[0])]
    for i in range(4):
        j = (i + 1) % 4
        q.append((front[i], back[i], back[j], front[j]))
    tris = []
    for a, b, c, d in q:
        tris += [(a, b, c), (a, c, d)]
    return outward(tris)


def box(lo, hi):
    x0, y0, z0 = lo
    x1, y1, z1 = hi
    return prism([(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0)],
                 [(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)])


def raked_grip():
    """Below the frame's back, its bottom further back (toward -x) than its top."""
    side = [(0.0, 0.0), (0.04, 0.0), (0.01, -0.1), (-0.03, -0.1)]
    return prism([(x, y, -0.011) for x, y in side], [(x, y, 0.011) for x, y in side])


def to_authored(p):
    """Y-up metres to the authored Z-up centimetres (the root node undoes it)."""
    x, y, z = p
    return (x * 100, -z * 100, y * 100)


def png_bytes(rgbs):
    raw = b'\0' + bytes(c for rgb in rgbs for c in rgb)

    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', len(rgbs), 1, 8, 2, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


TEXEL = (60, 60, 64)          # the slide's texel (sRGB); the other texel is red and unused
FLAT = {'Frame': (0.02, 0.02, 0.025), 'Light': (0.4, 0.4, 0.42), 'Spare': (0.8, 0.7, 0.1)}


def standin_glb(path):
    parts = [  # (node name, mesh name, material, triangles)
        ('Slide', 'Slide_mesh', 'Slide', box((0.0, 0.02, -0.012), (0.19, 0.05, 0.012))),
        ('Lower', 'Lower_mesh', 'Frame', box((0.02, 0.0, -0.011), (0.17, 0.02, 0.011)) + raked_grip() +
         box((0.055, -0.02, -0.003), (0.062, 0.0, 0.003))),
        ('Olight', 'Light_mesh', 'Light', box((0.12, -0.02, -0.012), (0.18, 0.0, 0.012))),
        ('Magazine_spare', 'Mag_mesh', 'Spare', box((-0.15, -0.1, -0.01), (-0.12, 0.0, 0.01))),
        ('Object_9', 'Loose_mesh', 'Spare', box((-0.25, -0.05, -0.01), (-0.22, 0.0, 0.01))),
    ]
    blob = bytearray()
    views, accessors, meshes, nodes = [], [], [], []
    materials = {'Slide': 0, 'Frame': 1, 'Light': 2, 'Spare': 3}

    def add(data, count, typ):
        views.append({'buffer': 0, 'byteOffset': len(blob), 'byteLength': len(data)})
        blob.extend(data)
        blob.extend(b'\0' * (-len(blob) % 4))
        accessors.append({'bufferView': len(views) - 1, 'componentType': 5126, 'count': count, 'type': typ})
        return len(accessors) - 1

    for node, mesh, mat, tris in parts:
        pts = [to_authored(p) for t in tris for p in t]
        pos = add(struct.pack(f'<{len(pts) * 3}f', *[c for p in pts for c in p]), len(pts), 'VEC3')
        accessors[pos]['min'] = [min(p[k] for p in pts) for k in range(3)]
        accessors[pos]['max'] = [max(p[k] for p in pts) for k in range(3)]
        attrs = {'POSITION': pos}
        if mat == 'Slide':
            attrs['TEXCOORD_0'] = add(struct.pack(f'<{len(pts) * 2}f', *([0.25, 0.5] * len(pts))), len(pts), 'VEC2')
        meshes.append({'name': mesh, 'primitives': [{'attributes': attrs, 'material': materials[mat]}]})
        nodes.append({'name': node, 'mesh': len(meshes) - 1})
    png = png_bytes([TEXEL, (200, 0, 0)])
    views.append({'buffer': 0, 'byteOffset': len(blob), 'byteLength': len(png)})
    blob.extend(png)
    blob.extend(b'\0' * (-len(blob) % 4))
    # -90 degrees about x (Z-up to Y-up), times 0.01 (column major)
    nodes.append({'name': 'Sketchfab_model', 'children': list(range(len(nodes))),
                  'matrix': [0.01, 0, 0, 0, 0, 0, -0.01, 0, 0, 0.01, 0, 0, 0, 0, 0, 1]})
    doc = {'asset': {'version': '2.0'}, 'scene': 0, 'scenes': [{'nodes': [len(nodes) - 1]}], 'nodes': nodes,
           'meshes': meshes, 'accessors': accessors, 'bufferViews': views, 'buffers': [{'byteLength': len(blob)}],
           'images': [{'bufferView': len(views) - 1, 'mimeType': 'image/png'}], 'textures': [{'source': 0}],
           'materials': [{'name': 'Slide', 'pbrMetallicRoughness': {'baseColorTexture': {'index': 0}}}] +
                        [{'name': n, 'pbrMetallicRoughness': {'baseColorFactor': list(FLAT[n]) + [1.0]}}
                         for n in ('Frame', 'Light', 'Spare')]}
    text = json.dumps(doc).encode()
    text += b' ' * (-len(text) % 4)
    with open(path, 'wb') as f:
        f.write(struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(text) + 8 + len(blob)))
        f.write(struct.pack('<II', len(text), 0x4E4F534A) + text + struct.pack('<II', len(blob), 0x004E4942) + blob)


# Its profile: in the turned model (the world's +x, forward, becomes -z: turned x = world z,
# y = world y, z = -world x), metres
STANDIN = {
    'forward': '+x', 'up': '+y',
    'drop_nodes': ['Magazine_spare'],
    'keep_box': [[-0.1, -0.2, -0.3], [0.1, 0.2, 0.08]],
    'length': 0.22,                        # slide front (z -0.19) to grip bottom (z +0.03): scale 1
    'bore': [0.0, 0.035],
    'grip': [-0.05, -0.005],
    'trigger_box': [[-0.01, -0.03, -0.07], [0.01, 0.005, -0.05]],
    'colours': 8,
    'triangles': 30000,
}
# how far along z the conversion slides it: the grip point (y -0.085 once the bore is at 0, z -0.005)
# onto the pistol grip line
SHIFT_Z = PISTOL_GRIP[1] - (-0.085 - PISTOL_GRIP[0]) * math.tan(math.radians(GRIP_RAKE_DEG)) + 0.005


def run(args, ok=True):
    rc = subprocess.run([sys.executable, '-I', MAKE_GUN] + args, capture_output=True, text=True)
    if ok:
        check(rc.returncode == 0, f'make_gun.py {" ".join(args)} runs ({rc.stderr.strip()[-400:]})')
    return rc


def srgb(c):
    return round(255 * (c * 12.92 if c <= 0.0031308 else 1.055 * c ** (1 / 2.4) - 0.055))


def check_pair(right, left, name):
    """The left file is the right one mirrored in x, winding flipped, pivot mirrored."""
    check(right['ic'] == left['ic'] and right['vc'] == left['vc'], f'{name}: both hands have the same counts')
    check(sorted((key((-v[0], v[1], v[2])), key((-v[3], v[4], v[5]))) for v in right['verts']) ==
          sorted((key(v[0:3]), key(v[3:6])) for v in left['verts']),
          f'{name}: every left vertex and normal is a right one mirrored in x')

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
          f'{name}: every left triangle is a right one mirrored with its winding flipped')
    rp, lp = right['comps'][1]['motion'][0:3], left['comps'][1]['motion'][0:3]
    check(close(lp, [-rp[0], rp[1], rp[2]]), f'{name}: the left trigger pivot is the right one mirrored ({lp})')


def check_components(m, name):
    """Body and trigger as the client animates them; returns the trigger's pivot."""
    check([c['name'] for c in m['comps']] == ['body', 'trigger'], f'{name}: components body, trigger')
    body, trig = m['comps']
    check(body['type'] == 0 and body['path'] == '', f'{name}: the body does not move')
    check(trig['path'] == '/input/trigger' and trig['type'] == 2, f'{name}: the trigger rotates, from /input/trigger')
    check(body['first'] + body['count'] == trig['first'] and trig['first'] + trig['count'] == m['ic'],
          f'{name}: the two components cover every index')
    mo = trig['motion']
    check(abs(abs(mo[3]) - 1) < 1e-5 and abs(mo[4]) < 1e-5 and abs(mo[5]) < 1e-5, f'{name}: trigger axis is x')
    check(mo[6] == 0 and -45 < mo[7] < -10, f'{name}: the range swings the foot back ({mo[6:8]})')
    check(all(abs(length(v[3:6]) - 1) < 1e-4 for v in m['verts']), f'{name}: unit normals')
    return mo[0:3]


def test_standin(work):
    glb = os.path.join(work, 'standin.glb')
    standin_glb(glb)
    prof = os.path.join(work, 'standin.json')
    with open(prof, 'w') as f:
        json.dump(STANDIN, f)
    out = os.path.join(work, 'out')
    rc = run(['--in', glb, '--profile', prof, out])
    check('84 triangles in the file, 60 of the pistol' in rc.stdout,
          f'the spare magazine and the loose part are left out ({rc.stdout.strip()})')
    try:
        right = parse_model(os.path.join(out, 'gun_right.model'))
        left = parse_model(os.path.join(out, 'gun_left.model'))
    except (AssertionError, OSError) as e:
        check(False, f'the client reads the stand-in\'s files ({e})')
        return
    pivot = check_components(right, 'stand-in')
    check(right['ic'] // 3 == 60 and right['comps'][1]['count'] // 3 == 12,
          f'stand-in: 60 triangles, the trigger\'s 12 ({right["ic"] // 3}, {right["comps"][1]["count"] // 3})')
    pts = [v[0:3] for v in right['verts']]
    lo, hi = bounds(pts)
    check(abs((hi[2] - lo[2]) - 0.22) < 1e-5, f'stand-in: scaled to its length ({hi[2] - lo[2]:.5f})')
    check(abs(lo[2] - (-0.19 + SHIFT_Z)) < 1e-5, f'stand-in: slid along the bore onto the grip line ({lo[2]:.5f})')
    front = [p for p in pts if abs(p[2] - lo[2]) < 1e-6]
    cx, cy = sum(p[0] for p in front) / len(front), sum(p[1] for p in front) / len(front)
    check(abs(cx) < 1e-5 and abs(cy) < 1e-5, f'stand-in: the barrel\'s front is centred on the aim ray ({cx}, {cy})')
    check(abs(hi[1] - 0.015) < 1e-5 and abs(lo[1] + 0.135) < 1e-5, f'stand-in: the bore at y = 0 ({lo[1]}..{hi[1]})')
    check(close(pivot, [0.0, -0.035, -0.062 + SHIFT_Z]), f'stand-in: trigger hinged at its top front ({pivot})')
    tlo, thi = bounds([p for tri in triangles(right, right['comps'][1]) for p, _ in tri])
    check(close(tlo, [-0.003, -0.055, -0.062 + SHIFT_Z]) and close(thi, [0.003, -0.035, -0.055 + SHIFT_Z]),
          f'stand-in: the trigger is the loose piece in the frame ({tlo} {thi})')
    # Colours: the slide's texel and the flat colours, each vertex on a palette texel
    tw = right['tw']
    texels = [tuple(right['colour'][4 * i:4 * i + 3]) for i in range(tw)]
    check(right['th'] == 1 and tw == 3, f'stand-in: three colours left (slide, frame, light): {texels}')
    want_c = [TEXEL, tuple(srgb(c) for c in FLAT['Frame']), tuple(srgb(c) for c in FLAT['Light'])]
    check(all(any(max(abs(a - b) for a, b in zip(w, t)) <= 2 for t in texels) for w in want_c),
          f'stand-in: the palette holds the texture\'s colour and the flat ones ({texels} vs {want_c})')
    check(all(abs(v[10] * tw - 0.5 - round(v[10] * tw - 0.5)) < 1e-4 for v in right['verts']),
          'stand-in: every vertex is on a palette texel')
    slide_top = [v for v in right['verts'] if abs(v[1] - 0.015) < 1e-6]
    k = round(slide_top[0][10] * tw - 0.5) if slide_top else -1
    check(0 <= k < tw and max(abs(a - b) for a, b in zip(texels[k], TEXEL)) <= 2,
          'stand-in: the slide is drawn in its texture\'s colour')
    check_pair(right, left, 'stand-in')

    # Refused: no trigger in the box, nothing kept
    for bad, words in ((dict(STANDIN, trigger_box=[[0.5, 0.5, 0.5], [0.6, 0.6, 0.6]]), 'trigger_box'),
                       (dict(STANDIN, keep_box=[[5, 5, 5], [6, 6, 6]]), 'nothing is left')):
        with open(prof, 'w') as f:
            json.dump(bad, f)
        rc = run(['--in', glb, '--profile', prof, os.path.join(work, 'bad')], ok=False)
        check(rc.returncode != 0 and words in rc.stderr, f'refused: {words} ({rc.stderr.strip()})')
    check(not os.path.exists(os.path.join(work, 'bad', 'gun_right.model')), 'a refused conversion writes nothing')
    rc = run(['--in', glb, '--profile', prof, '--inspect'], ok=False)
    check(rc.returncode == 0 and 'Magazine_spare' in rc.stdout, f'--inspect lists the nodes ({rc.stderr.strip()})')

    # Over the budget: simplified (only where numpy and pyfqmr are installed)
    try:
        import numpy  # noqa: F401
        import pyfqmr  # noqa: F401
        have = True
    except ImportError:
        have = False
    with open(prof, 'w') as f:
        json.dump(dict(STANDIN, triangles=30), f)
    rc = run(['--in', glb, '--profile', prof, os.path.join(work, 'small')], ok=False)
    if not have:
        check(rc.returncode != 0 and 'pyfqmr' in rc.stderr, 'over the budget without pyfqmr says what it needs')
        print('(numpy or pyfqmr is missing here: the simplifying path is not run)')
        return
    check(rc.returncode == 0, f'over the budget, it simplifies ({rc.stderr.strip()[-300:]})')
    if rc.returncode == 0:
        r2 = parse_model(os.path.join(work, 'small', 'gun_right.model'))
        l2 = parse_model(os.path.join(work, 'small', 'gun_left.model'))
        check_components(r2, 'simplified')
        check(r2['ic'] // 3 <= 30 and r2['comps'][1]['count'] // 3 == 12,
              f'simplified: within the budget, the trigger kept whole ({r2["ic"] // 3})')
        check_pair(r2, l2, 'simplified')


def test_committed():
    try:
        right = parse_model(os.path.join(COMMITTED, 'gun_right.model'))
        left = parse_model(os.path.join(COMMITTED, 'gun_left.model'))
    except (AssertionError, OSError) as e:
        check(False, f'the client reads the committed gun ({e})')
        return
    name = 'committed'
    pivot = check_components(right, name)
    check(1000 < right['ic'] // 3 <= 30000, f'{name}: at most 30000 triangles a hand ({right["ic"] // 3})')
    pts = [v[0:3] for v in right['verts']]
    lo, hi = bounds(pts)
    check(0.17 <= hi[2] - lo[2] <= 0.23, f'{name}: the length of a G17 ({hi[2] - lo[2]:.4f})')
    check(0.11 <= hi[1] - lo[1] <= 0.16 and 0.02 <= hi[0] - lo[0] <= 0.05,
          f'{name}: its height and width ({hi[1] - lo[1]:.4f} x {hi[0] - lo[0]:.4f})')
    check(abs(lo[0] + hi[0]) < 0.006, f'{name}: centred left to right ({lo[0]:.4f} {hi[0]:.4f})')
    check(lo[2] < -0.1 and lo[1] < -0.08, f'{name}: the barrel in front of the origin, the grip below it')
    # The muzzle on the aim ray: the front of the slide (the frontmost points above the light) round the axis
    top = [p for p in pts if p[1] > -0.012]
    front = min(p[2] for p in top)
    face = [p for p in top if p[2] < front + 0.004]
    fx = (min(p[0] for p in face) + max(p[0] for p in face)) / 2
    fy_lo, fy_hi = min(p[1] for p in face), max(p[1] for p in face)
    check(abs(fx) < 0.002 and fy_lo < 0.0 < fy_hi, f'{name}: the front of the slide straddles the aim ray '
          f'(x {fx:.4f}, y {fy_lo:.4f}..{fy_hi:.4f})')
    check(front < -0.09, f'{name}: the muzzle well in front of the origin ({front:.4f})')
    check(-0.04 < pivot[1] < 0.0 and -0.06 < pivot[2] < 0.03 and abs(pivot[0]) < 0.004,
          f'{name}: the trigger hinge is under the bore, ahead of the grip ({pivot})')
    check_pair(right, left, name)


def main():
    with tempfile.TemporaryDirectory() as work:
        test_standin(work)
    if COMMITTED:
        test_committed()
    print(f'{checks - failed}/{checks} checks passed')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
