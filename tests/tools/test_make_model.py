#!/usr/bin/env python3
"""Tests tools/xr/make_model.py, the glTF/GLB/OBJ to MXFM converter for custom controller models.
usage: test_make_model.py MAKE_MODEL.py MAKE_CONTROLLER.py

Tiny models are generated here (two boxes as an embedded glTF, an external-.bin glTF and a GLB; a cube
of quads as an OBJ), converted with the command line tool, and read back with a small MXFM parser that
mirrors rooms/room_gpu.c (mlxr_model_file_parse and the range checks of openTextured: header 32 bytes,
component records of 208 bytes, vertices of 12 floats, uint32 indices, two RGBA8 images). Checked:
vertex, index and component counts, the node transforms in the bounds and in every position, unit
normals and tangent frames, --scale, --mirror (x flipped in positions, normals and motion, winding
flipped so triangles still face the way their normals do, a mirrored rotation turning the mirrored
point), --component ranges and motion, quads and polygons triangulated, flat normals made, textures
(a palette of material colours, an embedded PNG, --tex with every PNG row filter and several colour
types), identical output from the three glTF containers, the error messages for what is not
supported, and the generic controller's glb converting."""
import base64
import json
import math
import os
import struct
import subprocess
import sys
import tempfile
import zlib

MAKE_MODEL, MAKE_CONTROLLER = sys.argv[1], sys.argv[2]
failed = 0
checks = 0


def check(cond, what):
    global failed, checks
    checks += 1
    if not cond:
        failed += 1
        print(f'FAIL: {what}')


def close(a, b, eps=1e-5):
    return all(abs(x - y) <= eps for x, y in zip(a, b)) and len(a) == len(b)


# --- the client's reader, in Python ------------------------------------------------------------------

VERTEX_FLOATS = 12
COMPONENT_BYTES = 208


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
        assert mtype <= (4 if version >= 2 else 3), 'motion type'
        comps.append({'name': name, 'path': path_in, 'first': first, 'count': count, 'type': mtype,
                      'motion': motion})
    floats = struct.unpack_from(f'<{vc * VERTEX_FLOATS}f', d, voff)
    verts = [floats[i * VERTEX_FLOATS:(i + 1) * VERTEX_FLOATS] for i in range(vc)]
    idx = struct.unpack_from(f'<{ic}I', d, ioff)
    assert all(i < vc for i in idx), 'index range'
    texels = tw * th
    colour = d[toff:toff + texels * 4]
    surface = d[toff + texels * 4:toff + texels * 8]
    return {'version': version, 'vc': vc, 'ic': ic, 'comps': comps, 'verts': verts, 'idx': idx,
            'tw': tw, 'th': th, 'colour': colour, 'surface': surface, 'bytes': d}


def sub(a, b): return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def cross(a, b): return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
def length(a): return math.sqrt(dot(a, a))


def bounds(m):
    pts = [v[0:3] for v in m['verts']]
    return [min(p[k] for p in pts) for k in range(3)], [max(p[k] for p in pts) for k in range(3)]


def faces_outward(m):
    """Every triangle's winding normal agrees with its vertices' normals."""
    bad = 0
    for t in range(0, m['ic'], 3):
        a, b, c = (m['verts'][i] for i in m['idx'][t:t + 3])
        n = cross(sub(b[0:3], a[0:3]), sub(c[0:3], a[0:3]))
        avg = [a[3 + k] + b[3 + k] + c[3 + k] for k in range(3)]
        if dot(n, avg) <= 0:
            bad += 1
    return bad == 0


def frames_ok(m):
    for v in m['verts']:
        if abs(length(v[3:6]) - 1) > 1e-4 or abs(length(v[6:9]) - 1) > 1e-4:
            return False
        if abs(dot(v[3:6], v[6:9])) > 1e-4 or v[9] not in (-1.0, 1.0):
            return False
    return True


def rotate_about(p, pivot, axis, degrees):
    a = [c / length(axis) for c in axis]
    r = math.radians(degrees)
    q = sub(p, pivot)
    c, s = math.cos(r), math.sin(r)
    k = cross(a, q)
    d = dot(a, q)
    return [pivot[i] + q[i] * c + k[i] * s + a[i] * d * (1 - c) for i in range(3)]


# --- tiny models -------------------------------------------------------------------------------------

FACES = [  # normal, u, v with u x v = normal: counter clockwise seen from outside
    ((1, 0, 0), (0, 1, 0), (0, 0, 1)), ((-1, 0, 0), (0, 0, 1), (0, 1, 0)),
    ((0, 1, 0), (0, 0, 1), (1, 0, 0)), ((0, -1, 0), (1, 0, 0), (0, 0, 1)),
    ((0, 0, 1), (1, 0, 0), (0, 1, 0)), ((0, 0, -1), (0, 1, 0), (1, 0, 0))]


def box(half):
    """(positions, normals, uvs, indices) of a box about the origin, 24 vertices, 12 triangles."""
    pos, nrm, uv, idx = [], [], [], []
    for n, u, v in FACES:
        base = len(pos)
        for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            pos.append(tuple(n[k] * half[k] + u[k] * su * half[k] + v[k] * sv * half[k] for k in range(3)))
            nrm.append(n)
            uv.append((0.5 + 0.5 * su * 0.9, 0.5 + 0.5 * sv * 0.9))
        idx += [base, base + 1, base + 2, base, base + 2, base + 3]
    return pos, nrm, uv, idx


BODY_HALF = (0.010, 0.030, 0.050)
TRIGGER_HALF = (0.004, 0.012, 0.006)
ROOT_T = (0.010, 0.0, 0.0)
BODY_T = (0.030, 0.005, 0.0)
TRIGGER_T = (0.0, -0.020, -0.030)
YROT = (0.0, math.sin(math.pi / 4), 0.0, math.cos(math.pi / 4))  # 90 degrees about y


def pack_mesh(parts):
    """Binary buffer, bufferViews and accessors for boxes [(pos, nrm, uv, idx)] in one buffer."""
    blob = b''
    views, accs, meshes = [], [], []
    for pos, nrm, uv, idx in parts:
        entry = {}
        for name, data, fmt, kind, ctype in (
                ('POSITION', pos, '3f', 'VEC3', 5126), ('NORMAL', nrm, '3f', 'VEC3', 5126),
                ('TEXCOORD_0', uv, '2f', 'VEC2', 5126), ('indices', idx, 'H', 'SCALAR', 5123)):
            raw = b''.join(struct.pack('<' + fmt, *e) if isinstance(e, tuple) else struct.pack('<' + fmt, e)
                           for e in data)
            raw += b'\0' * (-len(raw) % 4)
            views.append({'buffer': 0, 'byteOffset': len(blob), 'byteLength': len(raw)})
            blob += raw
            acc = {'bufferView': len(views) - 1, 'componentType': ctype, 'count': len(data), 'type': kind}
            if name == 'POSITION':
                acc['min'] = [min(p[k] for p in pos) for k in range(3)]
                acc['max'] = [max(p[k] for p in pos) for k in range(3)]
            accs.append(acc)
            entry[name] = len(accs) - 1
        meshes.append(entry)
    return blob, views, accs, meshes


def gltf_doc(blob, views, accs, meshes, materials=(0, 0), extra=None, uri=None):
    doc = {
        'asset': {'version': '2.0'}, 'scene': 0, 'scenes': [{'nodes': [0]}],
        'nodes': [{'name': 'Gun', 'translation': list(ROOT_T), 'children': [1, 2]},
                  {'name': 'Body', 'mesh': 0, 'translation': list(BODY_T)},
                  {'name': 'Trigger', 'mesh': 1, 'translation': list(TRIGGER_T), 'rotation': list(YROT)}],
        'meshes': [{'name': 'BodyMesh', 'primitives': [{'attributes': {k: v for k, v in meshes[0].items()
                                                                       if k != 'indices'},
                                                         'indices': meshes[0]['indices'],
                                                         'material': materials[0]}]},
                   {'name': 'TriggerMesh', 'primitives': [{'attributes': {k: v for k, v in meshes[1].items()
                                                                          if k != 'indices'},
                                                            'indices': meshes[1]['indices'],
                                                            'material': materials[1]}]}],
        'materials': [{'pbrMetallicRoughness': {'baseColorFactor': [0.8, 0.2, 0.1, 1.0]}},
                      {'pbrMetallicRoughness': {'baseColorFactor': [0.1, 0.1, 0.1, 1.0]}}],
        'accessors': accs, 'bufferViews': views,
        'buffers': [{'byteLength': len(blob)}],
    }
    if uri is not None:
        doc['buffers'][0]['uri'] = uri
    if extra:
        extra(doc)
    return doc


def two_boxes():
    return pack_mesh([box(BODY_HALF), box(TRIGGER_HALF)])


def write_gltf_embedded(path, **kw):
    blob, views, accs, meshes = two_boxes()
    uri = 'data:application/octet-stream;base64,' + base64.b64encode(blob).decode()
    doc = gltf_doc(blob, views, accs, meshes, uri=uri, **kw)
    with open(path, 'w') as f:
        json.dump(doc, f)


def write_gltf_external(path, **kw):
    blob, views, accs, meshes = two_boxes()
    with open(path[:-5] + '.bin', 'wb') as f:
        f.write(blob)
    doc = gltf_doc(blob, views, accs, meshes, uri=os.path.basename(path)[:-5] + '.bin', **kw)
    with open(path, 'w') as f:
        json.dump(doc, f)


def write_glb(path, **kw):
    blob, views, accs, meshes = two_boxes()
    doc = gltf_doc(blob, views, accs, meshes, **kw)
    text = json.dumps(doc).encode()
    text += b' ' * (-len(text) % 4)
    blob += b'\0' * (-len(blob) % 4)
    total = 12 + 8 + len(text) + 8 + len(blob)
    with open(path, 'wb') as f:
        f.write(struct.pack('<III', 0x46546C67, 2, total))
        f.write(struct.pack('<II', len(text), 0x4E4F534A) + text)
        f.write(struct.pack('<II', len(blob), 0x004E4942) + blob)


def expected_positions():
    """The two boxes' corners after the node transforms, worked out here independently: the body is
    moved by its node and the root; the trigger is turned 90 degrees about y (x, z -> z, -x) first."""
    out = []
    for p in box(BODY_HALF)[0]:
        out.append(tuple(p[k] + BODY_T[k] + ROOT_T[k] for k in range(3)))
    for p in box(TRIGGER_HALF)[0]:
        r = (p[2], p[1], -p[0])
        out.append(tuple(r[k] + TRIGGER_T[k] + ROOT_T[k] for k in range(3)))
    return out


# --- PNGs ----------------------------------------------------------------------------------------------

def png(width, height, rows, ctype=2, depth=8, palette=None, filters=None, interlace=0):
    """rows: lists of raw scanline bytes. Filters each row by filters[y % len] (0..4)."""
    bits = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype] * depth
    bpp = max(1, bits // 8)
    out = b''
    prev = bytes(len(rows[0]))
    for y, row in enumerate(rows):
        f = (filters or [0])[y % len(filters or [0])]
        res = bytearray()
        for i, x in enumerate(row):
            a = row[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 0:
                pred = 0
            elif f == 1:
                pred = a
            elif f == 2:
                pred = b
            elif f == 3:
                pred = (a + b) >> 1
            else:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
            res.append((x - pred) & 255)
        out += bytes([f]) + bytes(res)
        prev = row

    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body))
    data = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, depth, ctype, 0, 0, interlace))
    if palette:
        data += chunk(b'PLTE', palette)
    return data + chunk(b'IDAT', zlib.compress(out)) + chunk(b'IEND', b'')


W, H = 5, 5
PIXELS = [[(x * 40 + 3, y * 50 + 7, (x * y * 11 + 90) & 255) for x in range(W)] for y in range(H)]
RGB_ROWS = [bytes(c for px in row for c in px) for row in PIXELS]
RGB_EXPECT = b''.join(bytes([px[0], px[1], px[2], 255]) for row in PIXELS for px in row)


# --- OBJ -----------------------------------------------------------------------------------------------

CUBE_V = [(-1, -1, -1), (1, -1, -1), (1, 1, -1), (-1, 1, -1), (-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1)]
CUBE_Q = [(4, 3, 2, 1), (5, 6, 7, 8), (1, 2, 6, 5), (2, 3, 7, 6), (3, 4, 8, 7), (4, 1, 5, 8)]  # outward, ccw
CUBE_N = [(0, 0, -1), (0, 0, 1), (0, -1, 0), (1, 0, 0), (0, 1, 0), (-1, 0, 0)]


def cube_obj(normals=True, objects=False):
    lines = ['# a cube of quads, centred, half size 0.02', 'mtllib x.mtl']
    for v in CUBE_V:
        lines.append('v ' + ' '.join(f'{c * 0.02}' for c in v))
    lines += ['vt 0 0', 'vt 1 0', 'vt 1 1', 'vt 0 1']
    if normals:
        for n in CUBE_N:
            lines.append('vn ' + ' '.join(map(str, n)))
    for k, q in enumerate(CUBE_Q):
        if objects and k == 0:
            lines.append('o Trigger')
        elif objects and k == 1:
            lines.append('o Rest')
        refs = []
        for corner, vi in enumerate(q):
            vi_ref = vi if k % 2 == 0 else vi - 9  # half of them with negative (relative) indices
            vt = corner + 1 if k % 2 == 0 else corner + 1 - 5
            vn = k + 1 if k % 2 == 0 else k + 1 - 7
            refs.append(f'{vi_ref}/{vt}/{vn}' if normals else f'{vi_ref}/{vt}')
        lines.append('f ' + ' '.join(refs))
    return '\n'.join(lines) + '\n'


# --- running the tool ----------------------------------------------------------------------------------

def run(*args):
    p = subprocess.run([sys.executable, '-I', MAKE_MODEL, *args], capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


def convert_ok(work, name, src, *extra):
    out = os.path.join(work, name)
    rc, text = run('--in', src, '--out', out, *extra)
    check(rc == 0, f'{name}: converts ({text.strip()})')
    if rc != 0:
        return None
    try:
        return parse_model(out)
    except AssertionError as e:
        check(False, f'{name}: the client would refuse it ({e})')
        return None


def convert_fails(work, name, src, expect, *extra):
    out = os.path.join(work, name)
    rc, text = run('--in', src, '--out', out, *extra)
    check(rc != 0 and expect.lower() in text.lower(), f'{name}: refused with "{expect}" (got rc {rc}: {text.strip()})')
    check(not os.path.exists(out), f'{name}: nothing written')


def pos_set(m):
    return sorted(tuple(round(c, 5) for c in v[0:3]) for v in m['verts'])


def main():
    with tempfile.TemporaryDirectory() as work:
        gltf = os.path.join(work, 'gun.gltf')
        write_gltf_embedded(gltf)

        # --- the plain glTF
        m = convert_ok(work, 'plain.model', gltf)
        want = expected_positions()
        if m:
            check(m['version'] == 2, 'MXFM version 2')
            check(m['vc'] == 48 and m['ic'] == 72, f'48 vertices, 72 indices (got {m["vc"]}, {m["ic"]})')
            check(len(m['comps']) == 1 and m['comps'][0]['name'] == 'body' and m['comps'][0]['first'] == 0
                  and m['comps'][0]['count'] == 72 and m['comps'][0]['type'] == 0 and m['comps'][0]['path'] == '',
                  'one still body component over every index')
            lo, hi = bounds(m)
            check(close(lo, [min(p[k] for p in want) for k in range(3)]) and
                  close(hi, [max(p[k] for p in want) for k in range(3)]),
                  f'bounds follow the node transforms ({lo} {hi})')
            check(pos_set(m) == sorted(tuple(round(c, 5) for c in p) for p in want), 'every position is where the maths says')
            check(frames_ok(m), 'unit normals and tangent frames')
            check(faces_outward(m), 'windings agree with normals')
            check((m['tw'], m['th']) == (1, 1) and m['colour'][3] == 255, 'a 1x1 colour image with opaque alpha')
            # 0.8, 0.2, 0.1 linear -> sRGB
            check(tuple(m['colour'][:3]) == (231, 124, 89), f'baseColorFactor in sRGB ({tuple(m["colour"][:3])})')
            check(m['surface'] == bytes([128, 128, 255, 128]), 'flat normal map and middling roughness')
            # the trigger box (transformed 90 degrees) kept its UVs: v runs from the top as glTF's does
            check(all(0.0 <= v[10] <= 1.0 and 0.0 <= v[11] <= 1.0 for v in m['verts']), 'UVs in range')

        # --- the three containers give the same file
        glb = os.path.join(work, 'gun.glb')
        write_glb(glb)
        ext_dir = os.path.join(work, 'ext')
        os.makedirs(ext_dir)
        ext = os.path.join(ext_dir, 'gun.gltf')
        write_gltf_external(ext)
        m_glb = convert_ok(work, 'glb.model', glb)
        m_ext = convert_ok(work, 'ext.model', ext)
        if m and m_glb and m_ext:
            check(m['bytes'] == m_glb['bytes'], 'a GLB converts to the same bytes as the embedded glTF')
            check(m['bytes'] == m_ext['bytes'], 'an external .bin converts to the same bytes too')

        # --- components
        spec = 'trigger=Trigger,input=/input/trigger'
        c = convert_ok(work, 'comp.model', gltf, '--component', spec)
        if c:
            names = [x['name'] for x in c['comps']]
            check(names == ['body', 'trigger'], f'body then trigger ({names})')
            body, trig = c['comps']
            check(body['first'] == 0 and body['count'] == 36 and trig['first'] == 36 and trig['count'] == 36,
                  'index ranges: body then trigger')
            check(trig['path'] == '/input/trigger' and trig['type'] == 2, 'rotate driven by /input/trigger')
            tm = trig['motion']
            tp = [p for p in want[24:]]
            top = max(p[1] for p in tp)
            cx = (min(p[0] for p in tp) + max(p[0] for p in tp)) / 2
            cz = (min(p[2] for p in tp) + max(p[2] for p in tp)) / 2
            check(close(tm[0:3], [cx, top, cz]), f'default pivot is the top centre of the trigger ({tm[0:3]})')
            check(close(tm[3:6], [1, 0, 0]) and close(tm[6:8], [0, -25]), 'default axis x and range 0 to -25 degrees')
            check(math.isnan(tm[8]), 'no pressed override (NaN)')
            check(all(x == 0 for x in body['motion'][0:8]) and body['type'] == 0, 'the body does not move')
            check(trig['first'] % 3 == 0, 'whole triangles')
            # every trigger index uses the trigger's own vertices, which sit at the trigger's place
            tv = {i for i in c['idx'][trig['first']:trig['first'] + trig['count']]}
            check(all(abs(c['verts'][i][1] - TRIGGER_T[1] - ROOT_T[1]) <= 0.0121 for i in tv), 'trigger range holds the trigger mesh')
            check(frames_ok(c) and faces_outward(c), 'components keep frames and windings')

        # --- scale and mirror
        s = convert_ok(work, 'scaled.model', gltf, '--scale', '2')
        if s and m:
            lo, hi = bounds(s)
            lo1, hi1 = bounds(m)
            check(close(lo, [2 * x for x in lo1]) and close(hi, [2 * x for x in hi1]), '--scale 2 doubles the bounds')
            check(frames_ok(s), 'scaled normals are still unit')
        mir_spec = 'trigger=Trigger,input=/input/trigger,axis=y,pivot=0.02:-0.01:-0.03,range=0:30'
        r = convert_ok(work, 'right.model', gltf, '--component', mir_spec)
        lft = convert_ok(work, 'left.model', gltf, '--component', mir_spec, '--mirror')
        if r and lft:
            check(r['vc'] == lft['vc'] and r['ic'] == lft['ic'], 'mirror keeps the counts')
            check(pos_set(lft) == sorted((round(-p[0], 5) + 0.0, p[1], p[2]) for p in pos_set(r)),
                  'mirror flips x of every position')
            def vset(model):
                return sorted(tuple(round(c, 5) + 0.0 for c in v[0:6] + v[10:12]) for v in model['verts'])
            check(vset(lft) == sorted(tuple(round(c, 5) + 0.0 for c in (-v[0], v[1], v[2], -v[3], v[4], v[5], v[10], v[11]))
                                      for v in r['verts']),
                  'mirror flips x of positions and normals and keeps the UVs')
            check(faces_outward(r) and faces_outward(lft), 'mirror flips the winding with it (faces still outward)')
            check(frames_ok(lft), 'mirrored tangent frames are unit and square')

            def tris(model, flip):
                out = []
                for t in range(0, model['ic'], 3):
                    p3 = [tuple(round(c, 5) + 0.0 for c in model['verts'][i][0:3]) for i in model['idx'][t:t + 3]]
                    out.append(tuple(p3))
                return out
            right_tris = [tuple((round(-p[0], 5) + 0.0, p[1], p[2]) for p in (t[0], t[2], t[1])) for t in tris(r, False)]
            check(sorted(right_tris) == sorted(tris(lft, True)), 'every triangle is the mirror of one, corners 1 and 2 swapped')
            rm, lm = r['comps'][1]['motion'], lft['comps'][1]['motion']
            check(close(rm[0:3], [0.02, -0.01, -0.03]) and close(rm[3:6], [0, 1, 0]), 'authored pivot and axis kept')
            check(close(lm[0:3], [-0.02, -0.01, -0.03]) and close(lm[3:6], [0, -1, 0]), 'mirrored pivot x flipped, y axis reversed')
            # a point on the trigger turned in the right model, mirrored, equals the mirrored point turned
            p = [0.013, -0.012, -0.04]
            turned = rotate_about(p, rm[0:3], rm[3:6], 20)
            mirrored_turned = rotate_about([-p[0], p[1], p[2]], lm[0:3], lm[3:6], 20)
            check(close(mirrored_turned, [-turned[0], turned[1], turned[2]], 1e-9), 'mirrored motion turns the mirrored point')
            # an x axis rotation is unchanged by the mirror
            xr = convert_ok(work, 'xr.model', gltf, '--component', 'trigger=Trigger,input=/input/trigger')
            xl = convert_ok(work, 'xl.model', gltf, '--component', 'trigger=Trigger,input=/input/trigger', '--mirror')
            if xr and xl:
                check(close(xl['comps'][1]['motion'][3:6], [1, 0, 0]), 'x axis stays x under the mirror')
                check(close(xl['comps'][1]['motion'][0:3], [-xr['comps'][1]['motion'][0], xr['comps'][1]['motion'][1],
                                                              xr['comps'][1]['motion'][2]]), 'default pivot mirrored with the part')
        tr = convert_ok(work, 'translate.model', gltf, '--component',
                        'trigger=Trigger,input=/input/grip,motion=translate,axis=0:0:1')
        if tr:
            check(tr['comps'][1]['type'] == 1 and close(tr['comps'][1]['motion'][3:6], [0, 0, 1])
                  and close(tr['comps'][1]['motion'][6:8], [0, -0.008]), 'translate component with default range')

        # --- materials: a palette of flat colours
        two = os.path.join(work, 'two.gltf')
        write_gltf_embedded(two, materials=(0, 1))
        t2 = convert_ok(work, 'palette.model', two)
        if t2:
            check((t2['tw'], t2['th']) == (2, 1), 'two flat materials make a 2x1 palette')
            check(t2['colour'] == bytes([231, 124, 89, 255, 89, 89, 89, 255]), f'palette colours ({list(t2["colour"])})')
            us = sorted({round(v[10], 6) for v in t2['verts']})
            check(us == [0.25, 0.75] and {round(v[11], 6) for v in t2['verts']} == {0.5},
                  f'each triangle sits on its material\'s texel centre ({us})')

        # --- textures
        pngs = {
            'rgb': (png(W, H, RGB_ROWS, filters=[0, 1, 2, 3, 4]), RGB_EXPECT),
            'rgba': (png(W, H, [bytes(c for px in row for c in (px + (77,))) for row in PIXELS], ctype=6,
                         filters=[4, 3, 2, 1, 0]), RGB_EXPECT),
            'grey': (png(W, H, [bytes(px[0] for px in row) for row in PIXELS], ctype=0, filters=[1, 4]),
                     b''.join(bytes([px[0]] * 3 + [255]) for row in PIXELS for px in row)),
            'rgb16': (png(W, H, [bytes(c for px in row for v in px for c in (v, 200)) for row in PIXELS], depth=16,
                          filters=[2]), RGB_EXPECT),
        }
        pal = bytes([10, 20, 30, 200, 100, 50, 0, 255, 0, 255, 255, 255])
        idx_rows = [bytes(((y + x) % 4) for x in range(W)) for y in range(H)]
        pngs['palette8'] = (png(W, H, idx_rows, ctype=3, palette=pal, filters=[0]),
                            b''.join(bytes(pal[v * 3:v * 3 + 3] + b'\xff') for row in idx_rows for v in row))
        packed = []
        for row in idx_rows:  # 2 bits per pixel
            bits = ''.join(f'{v:02b}' for v in row)
            bits += '0' * (-len(bits) % 8)
            packed.append(bytes(int(bits[i:i + 8], 2) for i in range(0, len(bits), 8)))
        pngs['palette2'] = (png(W, H, packed, ctype=3, depth=2, palette=pal, filters=[0, 2]), pngs['palette8'][1])
        obj = os.path.join(work, 'cube.obj')
        open(obj, 'w').write(cube_obj())
        for name, (data, expect) in pngs.items():
            path = os.path.join(work, name + '.png')
            open(path, 'wb').write(data)
            t = convert_ok(work, name + '.model', obj, '--tex', path)
            if t:
                check((t['tw'], t['th']) == (W, H), f'--tex {name}: size')
                check(t['colour'] == expect, f'--tex {name}: pixels decoded (colour, alpha 255)')
                check(t['surface'] == bytes([128, 128, 255, 128]) * (W * H), f'--tex {name}: flat surface image')
        # a glTF's own embedded PNG, used without --tex
        def textured(doc):
            doc['images'] = [{'uri': 'data:image/png;base64,' + base64.b64encode(pngs['rgb'][0]).decode()}]
            doc['textures'] = [{'source': 0}]
            doc['materials'][0] = {'pbrMetallicRoughness': {'baseColorTexture': {'index': 0}}}
            doc['materials'][1] = doc['materials'][0]
        tex_gltf = os.path.join(work, 'textured.gltf')
        write_gltf_embedded(tex_gltf, extra=textured)
        t = convert_ok(work, 'embedded_png.model', tex_gltf)
        if t:
            check(t['colour'] == RGB_EXPECT and (t['tw'], t['th']) == (W, H), 'a glTF\'s own PNG is the colour image')
            check(all(abs(v[10] - 0.5) <= 0.45 + 1e-6 and abs(v[11] - 0.5) <= 0.45 + 1e-6 for v in t['verts']),
                  "a textured material keeps the model's own UVs")

        # --- OBJ
        o = convert_ok(work, 'obj.model', obj)
        if o:
            check(o['vc'] == 24 and o['ic'] == 36, f'OBJ cube of 6 quads: 24 vertices, 12 triangles (got {o["vc"]}, {o["ic"]})')
            lo, hi = bounds(o)
            check(close(lo, [-0.02] * 3) and close(hi, [0.02] * 3), 'OBJ bounds')
            check(frames_ok(o) and faces_outward(o), 'OBJ unit normals, outward windings')
            check(all(0 <= v[10] <= 1 and 0 <= v[11] <= 1 for v in o['verts']), 'OBJ UVs in range')
            # the first quad's first corner: vt 0 0 -> u 0, v turned to run from the image top: 1
            pngf = os.path.join(work, 'rgb.png')
            ot = convert_ok(work, 'obj_uv.model', obj, '--tex', pngf)
            if ot:
                first = ot['verts'][ot['idx'][0]]
                check(close(first[10:12], [0.0, 1.0]), f'OBJ vt turned to run down the image ({first[10:12]})')
            check((o['tw'], o['th']) == (1, 1) and tuple(o['colour']) == (255, 255, 255, 255), 'OBJ without a texture is white')
        flat = os.path.join(work, 'flat.obj')
        open(flat, 'w').write(cube_obj(normals=False))
        f = convert_ok(work, 'flat.model', flat)
        if f:
            check(f['vc'] == 24 and frames_ok(f) and faces_outward(f), 'OBJ with no normals gets unit flat ones')
            check(all(dot(v[3:6], v[0:3]) > 0 for v in f['verts']), 'flat normals point out of the cube')
        mirrored = convert_ok(work, 'objm.model', obj, '--mirror', '--scale', '10')
        if mirrored and o:
            check(close(bounds(mirrored)[1], [0.2] * 3) and faces_outward(mirrored), 'OBJ scaled and mirrored')
        objs = os.path.join(work, 'objs.obj')
        open(objs, 'w').write(cube_obj(objects=True))
        oc = convert_ok(work, 'objc.model', objs, '--component', 'trigger=Trigger,input=/input/trigger')
        if oc:
            check([x['name'] for x in oc['comps']] == ['body', 'trigger'] and oc['comps'][1]['count'] == 6
                  and oc['comps'][0]['count'] == 30, 'an OBJ object name picks the component (its one quad: 6 indices, the other 5 quads: 30)')
        pent = os.path.join(work, 'pent.obj')
        with open(pent, 'w') as fh:
            for k in range(5):
                a = 2 * math.pi * k / 5
                fh.write(f'v {0.01 * math.cos(a)} {0.01 * math.sin(a)} 0\n')
            fh.write('vn 0 0 1\nf 1//1 2//1 3//1 4//1 5//1\n')
        p5 = convert_ok(work, 'pent.model', pent)
        if p5:
            check(p5['vc'] == 5 and p5['ic'] == 9 and faces_outward(p5), 'a pentagon is fanned into 3 triangles')

        # --- what is refused
        def mod(fn, name):
            path = os.path.join(work, name + '.gltf')
            write_gltf_embedded(path, extra=fn, materials=(0, 1))
            return path
        convert_fails(work, 'skin.model', mod(lambda d: d['nodes'][1].update(skin=0), 'skin'), 'skin')
        convert_fails(work, 'morph.model', mod(lambda d: d['meshes'][0]['primitives'][0].update(
            targets=[{'POSITION': 0}]), 'morph'), 'morph')
        convert_fails(work, 'draco.model', mod(lambda d: d.update(extensionsRequired=['KHR_draco_mesh_compression']),
                                               'draco'), 'draco')
        convert_fails(work, 'lines.model', mod(lambda d: d['meshes'][0]['primitives'][0].update(mode=1), 'lines'), 'lines')
        convert_fails(work, 'sparse.model', mod(lambda d: d['accessors'][0].update(sparse={'count': 1}), 'sparse'), 'sparse')
        convert_fails(work, 'short.model', mod(lambda d: d['accessors'][0].update(count=100000), 'short'), 'runs past')
        convert_fails(work, 'outside.model', mod(lambda d: d['buffers'][0].update(uri='../x.bin'), 'outside'), 'outside')

        def two_textures(doc):
            png_uri = 'data:image/png;base64,' + base64.b64encode(pngs['rgb'][0]).decode()
            doc['images'] = [{'uri': png_uri}, {'uri': png_uri}]
            doc['textures'] = [{'source': 0}, {'source': 1}]
            doc['materials'][0] = {'pbrMetallicRoughness': {'baseColorTexture': {'index': 0}}}
            doc['materials'][1] = {'pbrMetallicRoughness': {'baseColorTexture': {'index': 1}}}
        convert_fails(work, 'twotex.model', mod(two_textures, 'twotex'), 'several textures')

        def mixed(doc):
            two_textures(doc)
            doc['materials'][1] = {'pbrMetallicRoughness': {'baseColorFactor': [1, 0, 0, 1]}}
        convert_fails(work, 'mixed.model', mod(mixed, 'mixed'), 'flat colours')

        def jpeg(doc):
            doc['images'] = [{'uri': 'data:image/jpeg;base64,/9j/4AAQ'}]
            doc['textures'] = [{'source': 0}]
            doc['materials'][0] = {'pbrMetallicRoughness': {'baseColorTexture': {'index': 0}}}
            doc['materials'][1] = doc['materials'][0]
        convert_fails(work, 'jpeg.model', mod(jpeg, 'jpeg'), 'not a PNG')
        # --tex makes up for several materials, the atlas is the user's
        a = convert_ok(work, 'atlas.model', mod(two_textures, 'atlas'), '--tex', os.path.join(work, 'rgb.png'))
        if a:
            check(a['colour'] == RGB_EXPECT, '--tex overrides the model\'s materials')
        convert_fails(work, 'nocomp.model', gltf, 'no node or object named', '--component', 'a=Nope')
        convert_fails(work, 'badinput.model', gltf, 'not one the client reads', '--component', 'a=Trigger,input=/input/nope')
        convert_fails(work, 'bodyname.model', gltf, 'body', '--component', 'body=Trigger')
        convert_fails(work, 'dup.model', gltf, 'differ', '--component', 'a=Trigger', '--component', 'a=Body')
        convert_fails(work, 'axis.model', gltf, 'need input', '--component', 'a=Trigger,axis=y')
        convert_fails(work, 'scale0.model', gltf, 'scale', '--scale', '0')
        open(os.path.join(work, 'x.fbx'), 'w').write('x')
        convert_fails(work, 'fbx.model', os.path.join(work, 'x.fbx'), 'expected a')
        convert_fails(work, 'missing.model', os.path.join(work, 'missing.glb'), 'cannot read')
        convert_fails(work, 'nopng.model', obj, 'not a PNG', '--tex', obj)
        noscale = os.path.join(work, 'nouv.obj')
        open(noscale, 'w').write('v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n')
        convert_fails(work, 'nouv.model', noscale, 'no texture coordinates', '--tex', os.path.join(work, 'rgb.png'))
        interlaced = os.path.join(work, 'interlaced.png')
        open(interlaced, 'wb').write(png(W, H, RGB_ROWS, interlace=1))
        convert_fails(work, 'interlaced.model', obj, 'interlaced', '--tex', interlaced)
        badobj = os.path.join(work, 'bad.obj')
        open(badobj, 'w').write('v 0 0 0\nf 1 2 3\n')
        convert_fails(work, 'badobj.model', badobj, 'line 2')

        # --- the generic controller's own glb (vertex colours, one mesh, 20 segment tubes)
        ctl = os.path.join(work, 'controller.glb')
        rc = subprocess.run([sys.executable, '-I', MAKE_CONTROLLER, ctl], capture_output=True, text=True)
        check(rc.returncode == 0, 'make_controller.py runs')
        if rc.returncode == 0:
            g = open(ctl, 'rb').read()
            jlen = struct.unpack_from('<I', g, 12)[0]
            doc = json.loads(g[20:20 + jlen])
            tri_indices = doc['accessors'][3]['count']
            cm = convert_ok(work, 'controller.model', ctl)
            if cm:
                check(cm['ic'] == tri_indices, f'every triangle of the generic controller kept ({cm["ic"]} of {tri_indices})')
                check(cm['vc'] <= doc['accessors'][0]['count'] and frames_ok(cm), 'generic controller welded, unit normals')
                lo, hi = bounds(cm)
                want_lo, want_hi = doc['accessors'][0]['min'], doc['accessors'][0]['max']
                check(close(lo, want_lo, 1e-5) and close(hi, want_hi, 1e-5),
                      f'generic controller bounds are the glb bounds, in metres ({lo} {hi})')
                check(len(cm['comps']) == 1, 'one body component')

    print(f'{checks - failed}/{checks} checks passed')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
