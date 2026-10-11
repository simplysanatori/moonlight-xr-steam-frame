#!/usr/bin/env python3
"""Tests tools/frame-models/frame-controller-models.py: on a made-up pair of SteamVR render models
(a sphere body, a trigger and a marker, a flat colour texture) and, when this machine has SteamVR,
on the real Frame controller models. usage: test_converter.py CONVERTER

What is checked: both hands written as textured models (MXFM) that parse as the client reads them
(rooms/room_gpu.c mlxr_model_file_parse and its range checks), every triangle kept (and the debugging
budget still simplifying), the grip transform (the grip's own origin and rotation land on the origin,
the trigger component where the maths says), unit normals and tangent frames, texture coordinates
turned to run from the image's top row, the textures (colour with AO in alpha, the normal map with
DirectX's green turned to OpenGL's and roughness in alpha, flat stand-ins for missing optional maps,
the size knob), bounding boxes in metres, the older painted pair removed, and that a bad source
writes nothing (both hands or neither)."""
import json
import math
import os
import struct
import subprocess
import sys
import tempfile

CONVERTER = sys.argv[1]
failed = 0
checks = 0


def check(cond, what):
    global failed, checks
    checks += 1
    if not cond:
        failed += 1
        print(f'FAIL: {what}')


def rotation(deg):
    """The converter's rotate_xyz (X then Y then Z), rows."""
    x, y, z = (math.radians(d) for d in deg)
    rx = [[1, 0, 0], [0, math.cos(x), -math.sin(x)], [0, math.sin(x), math.cos(x)]]
    ry = [[math.cos(y), 0, math.sin(y)], [0, 1, 0], [-math.sin(y), 0, math.cos(y)]]
    rz = [[math.cos(z), -math.sin(z), 0], [math.sin(z), math.cos(z), 0], [0, 0, 1]]
    mul = lambda a, b: [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
    return mul(rz, mul(ry, rx))


VERTEX_FLOATS = 12
COMPONENT_BYTES = 208


def parse_model(path):
    """mlxr_model_file_parse's rules and the loader's range checks; returns a dict or raises
    AssertionError."""
    d = open(path, 'rb').read()
    assert d[:4] == b'MXFM', 'magic'
    version, vc, ic, cc, tw, th, flags = struct.unpack_from('<7I', d, 4)
    assert version in (1, 2) and flags == 0, 'version'
    assert 0 < vc < 1 << 22 and 0 < ic <= 1 << 24 and ic % 3 == 0 and 0 < cc <= 32, 'counts'
    assert 0 < tw <= 8192 and 0 < th <= 8192, 'texture size'
    voff = 32 + cc * COMPONENT_BYTES
    ioff = voff + vc * VERTEX_FLOATS * 4
    toff = ioff + ic * 4
    assert len(d) == toff + 2 * tw * th * 4, 'size'
    comps = []
    for c in range(cc):
        o = 32 + c * COMPONENT_BYTES
        name = d[o:o + 32].split(b'\0')[0].decode()
        path_ = d[o + 32:o + 64].split(b'\0')[0].decode()
        first, count, kind, pad = struct.unpack_from('<4I', d, o + 64)
        motion = struct.unpack_from('<32f', d, o + 80)
        assert first + count <= ic and first % 3 == 0 and count % 3 == 0 and kind <= 3 and pad == 0, f'component {name}'
        comps.append({'name': name, 'path': path_, 'first': first, 'count': count, 'type': kind, 'motion': motion})
    verts = [struct.unpack_from('<12f', d, voff + i * 48) for i in range(vc)]
    idx = struct.unpack_from(f'<{ic}I', d, ioff)
    assert max(idx) < vc, 'index range'
    colour = d[toff:toff + tw * th * 4]
    surface = d[toff + tw * th * 4:]
    return {'verts': verts, 'idx': idx, 'comps': comps, 'w': tw, 'h': th, 'colour': colour, 'surface': surface}


def comp_verts(m, name):
    c = next(c for c in m['comps'] if c['name'] == name)
    return [m['verts'][i] for i in set(m['idx'][c['first']:c['first'] + c['count']])]


def bbox(verts):
    return [(min(v[a] for v in verts), max(v[a] for v in verts)) for a in range(3)]


# --- a made-up render model ----------------------------------------------------------------------

def sphere_obj(center, radius, rings, segs):
    v, vt, vn, f = [], [], [], []
    for i in range(rings + 1):
        for j in range(segs + 1):
            th, ph = math.pi * i / rings, 2 * math.pi * j / segs
            n = (math.sin(th) * math.cos(ph), math.cos(th), math.sin(th) * math.sin(ph))
            v.append(tuple(center[k] + radius * n[k] for k in range(3)))
            vt.append((j / segs, 1 - i / rings))
            vn.append(n)
    lines = ['v %.7f %.7f %.7f' % p for p in v] + ['vt %.6f %.6f' % t for t in vt] + ['vn %.6f %.6f %.6f' % n for n in vn]
    for i in range(rings):
        for j in range(segs):
            a = i * (segs + 1) + j + 1
            b, c, d = a + 1, a + segs + 1, a + segs + 2
            lines.append(f'f {a}/{a}/{a} {c}/{c}/{c} {b}/{b}/{b}')
            lines.append(f'f {b}/{b}/{b} {c}/{c}/{c} {d}/{d}/{d}')
    return '\n'.join(lines) + '\n'


def make_png(path, rgb):
    import gi
    gi.require_version('GdkPixbuf', '2.0')
    from gi.repository import GdkPixbuf, GLib
    pixels = GLib.Bytes.new(bytes(rgb) * (64 * 64))
    GdkPixbuf.Pixbuf.new_from_bytes(pixels, GdkPixbuf.Colorspace.RGB, False, 8, 64, 64, 64 * 3).savev(path, 'png', [], [])


GRIP_ORIGIN = (0.01, -0.02, 0.10)
GRIP_ROT = (30.0, 0.0, 0.0)
TRIGGER_AT = (0.01, -0.02, 0.15)  # 0.05 behind the grip origin along the model's z
BODY_AT = (0.01, -0.02, 0.10)     # centred on the grip origin


NORMAL = (100, 60, 230)  # DirectX green: the converter turns 60 into 195
ROUGH = 77


def make_rendermodels(root, ao_level, optional=True):
    for hand in ('left', 'right'):
        name = f'frame_controller_{hand}'
        d = os.path.join(root, name)
        os.makedirs(d)
        open(os.path.join(d, name + '_body.obj'), 'w').write(sphere_obj(BODY_AT, 0.03, 30, 40))
        open(os.path.join(d, name + '_trigger.obj'), 'w').write(sphere_obj(TRIGGER_AT, 0.01, 8, 10))
        open(os.path.join(d, name + '_status.obj'), 'w').write(sphere_obj((5, 5, 5), 0.01, 8, 10))  # hidden
        comp = lambda **k: {'component_local': {'origin': [0, 0, 0], 'rotate_xyz': [0, 0, 0]}, **k}
        comps = {
            'body': comp(filename=name + '_body.obj'),
            'trigger': comp(filename=name + '_trigger.obj'),
            'status': comp(filename=name + '_status.obj', visibility={'default': False}),
            'openxr_grip': {'component_local': {'origin': list(GRIP_ORIGIN), 'rotate_xyz': list(GRIP_ROT)}},
        }
        json.dump({'components': comps}, open(os.path.join(d, name + '.json'), 'w'))
        make_png(os.path.join(d, name + '_color.png'), (200, 100, 50))
        if optional:
            make_png(os.path.join(d, name + '_ao.png'), (ao_level,) * 3)
            make_png(os.path.join(d, name + '_normal.png'), NORMAL)
            make_png(os.path.join(d, name + '_rough.png'), (ROUGH,) * 3)


def run(args, **env):
    e = dict(os.environ)
    e.update({k: str(v) for k, v in env.items()})
    return subprocess.run([sys.executable, '-I', CONVERTER] + args, capture_output=True, text=True, env=e)


def main():
    try:
        import gi
        gi.require_version('GdkPixbuf', '2.0')
        from gi.repository import GdkPixbuf  # noqa: F401
    except (ImportError, ValueError):
        print('no python GdkPixbuf: skipped')
        return 77
    with tempfile.TemporaryDirectory() as tmp:
        src, out = os.path.join(tmp, 'rendermodels'), os.path.join(tmp, 'out')
        make_rendermodels(src, 128)

        # --- the made-up models -----------------------------------------------------------------
        os.makedirs(out)
        for hand in ('left', 'right'):  # an older converter's painted pair, to be removed
            open(os.path.join(out, f'frame_controller_{hand}.room'), 'w').write('old')
        r = run([out, src])
        check(r.returncode == 0, f'converter failed: {r.stdout}{r.stderr}')
        check(not [f for f in os.listdir(out) if f.endswith('.room')], 'the painted pair is removed')
        full = 2 * 30 * 40 + 2 * 8 * 10  # the visible triangles in the source
        rot = rotation(GRIP_ROT)
        for hand in ('left', 'right'):
            path = os.path.join(out, f'frame_controller_{hand}.model')
            check(os.path.isfile(path), f'{hand} written')
            try:
                m = parse_model(path)
            except (AssertionError, OSError, struct.error) as e:
                check(False, f'{hand} does not parse: {e}')
                continue
            verts, idx = m['verts'], m['idx']
            check(len(idx) // 3 == full, f'{hand}: every triangle kept ({len(idx) // 3} of {full})')
            names = [c['name'] for c in m['comps']]
            check(names == ['body', 'trigger'], f'{hand}: components body then trigger, hidden left out: {names}')
            check(sum(c['count'] for c in m['comps']) == len(idx) and m['comps'][1]['first'] == m['comps'][0]['count'],
                  f'{hand}: the components cover the indices in order')
            check(all(math.isfinite(x) for v in verts for x in v), f'{hand}: finite')
            # the body is centred on the grip origin, so its box is centred on 0 and 6 cm a side
            box = bbox(comp_verts(m, 'body'))
            for a in range(3):
                check(abs(sum(box[a]) / 2) < 0.002 and 0.055 < box[a][1] - box[a][0] < 0.065, f'{hand}: body box axis {a} {box[a]}')
            # the trigger sphere: model (0, 0, +0.05) from the grip origin -> R^T (0, 0, 0.05)
            want = [sum(rot[j][i] * d for j, d in enumerate((0, 0, 0.05))) for i in range(3)]
            tv = comp_verts(m, 'trigger')
            centre = [sum(v[a] for v in tv) / len(tv) for a in range(3)]
            check(math.dist(centre, want) < 0.002, f'{hand}: the trigger is where the grip rotation puts it {centre} vs {want}')
            check(max(abs(c) for v in verts for c in v[:3]) < 0.2, f'{hand}: hidden component left out')
            # tangent frames: unit normal, unit tangent across it, w a sign
            check(all(abs(math.sqrt(sum(c * c for c in v[3:6])) - 1) < 1e-3 for v in verts), f'{hand}: unit normals')
            check(all(abs(math.sqrt(sum(c * c for c in v[6:9])) - 1) < 1e-3 and abs(sum(v[3 + k] * v[6 + k] for k in range(3))) < 1e-3
                      and v[9] in (-1.0, 1.0) for v in verts), f'{hand}: unit tangents across the normals, w +-1')
            # texture coordinates from the image's top row: the sphere's top pole has OBJ v = 1, so 0 here
            up = [rot[1][i] for i in range(3)]  # the model's +y in grip space (R^T y)
            body = comp_verts(m, 'body')
            top = max(body, key=lambda v: sum(v[k] * up[k] for k in range(3)))
            bottom = min(body, key=lambda v: sum(v[k] * up[k] for k in range(3)))
            check(top[11] < 0.01 and bottom[11] > 0.99, f'{hand}: v runs from the top row (top {top[11]}, bottom {bottom[11]})')
            check(all(0.0 <= v[10] <= 1.0 and 0.0 <= v[11] <= 1.0 for v in verts), f'{hand}: uv in range')
            # the textures, 64 x 64 as made: colour + AO, normal (green turned) + roughness
            check((m['w'], m['h']) == (64, 64), f'{hand}: texture size {m["w"]}x{m["h"]}')
            check(m['colour'][:4] == bytes((200, 100, 50, 128)) and len(set(m['colour'][i:i + 4] for i in range(0, len(m['colour']), 4))) == 1,
                  f'{hand}: colour and AO {tuple(m["colour"][:4])}')
            check(m['surface'][:4] == bytes((NORMAL[0], 255 - NORMAL[1], NORMAL[2], ROUGH)), f'{hand}: normal (OpenGL green) and roughness {tuple(m["surface"][:4])}')

        # the texture size knob, and the debugging budget still simplifying
        out2 = os.path.join(tmp, 'out2')
        r = run([out2, src], MLXR_FRAME_MODEL_TEXTURE=32, MLXR_FRAME_MODEL_TRIANGLES=400)
        check(r.returncode == 0, f'knob run: {r.stderr}')
        try:
            m = parse_model(os.path.join(out2, 'frame_controller_left.model'))
            check((m['w'], m['h']) == (32, 32), f'MLXR_FRAME_MODEL_TEXTURE=32 gives {m["w"]}x{m["h"]}')
            tris = len(m['idx']) // 3
            check(tris < full and tris <= 400 * 1.5, f'budget of 400 from {full}: {tris}')
        except (AssertionError, OSError, struct.error) as e:
            check(False, f'knob output: {e}')

        # no AO, normal or roughness map: flat stand-ins (white AO, flat normal, mid roughness)
        src3, out3 = os.path.join(tmp, 'rendermodels3'), os.path.join(tmp, 'out3')
        make_rendermodels(src3, 0, optional=False)
        r = run([out3, src3])
        check(r.returncode == 0, f'colour-only source: {r.stderr}')
        try:
            m = parse_model(os.path.join(out3, 'frame_controller_right.model'))
            check(m['colour'][:4] == bytes((200, 100, 50, 255)) and m['surface'][:4] == bytes((128, 127, 255, 128)),
                  f'stand-ins {tuple(m["colour"][:4])} {tuple(m["surface"][:4])}')
        except (AssertionError, OSError, struct.error) as e:
            check(False, f'colour-only output: {e}')

        # --- bad sources write nothing, both hands or neither -------------------------------------
        for label, make in (('missing directory', None), ('corrupt json', 'json'), ('missing mesh', 'obj'),
                            ('missing texture', 'png')):
            bad, bout = os.path.join(tmp, 'bad_' + label.replace(' ', '_')), os.path.join(tmp, 'bout_' + label.replace(' ', '_'))
            if make is None:
                bad = os.path.join(tmp, 'nothing here')
            else:
                make_rendermodels(bad, 255)
                d = os.path.join(bad, 'frame_controller_right')  # the second hand fails
                if make == 'json':
                    open(os.path.join(d, 'frame_controller_right.json'), 'w').write('{ not json')
                elif make == 'obj':
                    os.remove(os.path.join(d, 'frame_controller_right_trigger.obj'))
                else:
                    os.remove(os.path.join(d, 'frame_controller_right_color.png'))
            r = run([bout, bad])
            check(r.returncode != 0 and r.stderr.strip() != '', f'{label}: fails with a message ({r.returncode})')
            left_over = os.listdir(bout) if os.path.isdir(bout) else []
            check(not left_over, f'{label}: nothing written, found {left_over}')

        # --- the real Frame models, when SteamVR is here ------------------------------------------
        real = '/opt/steamvr/drivers/frame_controller/resources/rendermodels'
        if os.path.isdir(real):
            rout = os.path.join(tmp, 'real')
            r = run([rout, real])
            check(r.returncode == 0, f'real models: {r.stdout}{r.stderr}')
            for hand in ('left', 'right'):
                try:
                    m = parse_model(os.path.join(rout, f'frame_controller_{hand}.model'))
                except (AssertionError, OSError, struct.error) as e:
                    check(False, f'real {hand}: {e}')
                    continue
                verts, idx = m['verts'], m['idx']
                tris = len(idx) // 3
                box = bbox(verts)
                print(f'real {hand}: {tris} triangles, {len(verts)} vertices, {len(m["comps"])} components, textures '
                      f'{m["w"]}x{m["h"]}, box {[tuple(round(c, 3) for c in b) for b in box]}')
                check(8000 <= tris <= 60000, f'real {hand}: triangle count {tris}')
                check(len(verts) < 60000, f'real {hand}: vertex count')
                check(m['comps'][0]['name'] == 'body' and any(c['name'] == 'trigger' for c in m['comps']), f'real {hand}: components')
                check(m['w'] >= 512 and m['w'] == m['h'], f'real {hand}: textures {m["w"]}x{m["h"]}')
                # a handheld controller, in metres around its grip: 5 to 25 cm long, nothing past 20 cm of the grip
                ext = [b[1] - b[0] for b in box]
                check(0.05 < max(ext) < 0.25 and all(-0.2 < c < 0.2 for b in box for c in b), f'real {hand}: size {ext}')
                # handedness: the thumbstick side. The right hand's box leans to -x of the grip, the left's to +x
                lean = (box[0][0] + box[0][1]) / 2
                check((lean < 0) == (hand == 'right'), f'real {hand}: leans the right way ({lean:.4f})')
        else:
            print('no SteamVR here: the real models were not tested')
    print('frame models converter: %d checks, %s' % (checks, 'FAILED %d' % failed if failed else 'ok'))
    return 1 if failed else 0


sys.exit(main())
