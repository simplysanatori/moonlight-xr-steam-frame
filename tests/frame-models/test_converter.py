#!/usr/bin/env python3
"""Tests tools/frame-models/frame-controller-models.py: on a made-up pair of SteamVR render models
(a sphere body, a trigger and a marker, a flat colour texture) and, when this machine has SteamVR,
on the real Frame controller models. usage: test_converter.py CONVERTER

What is checked: both hands written and both parse as the client reads them (the rules of
xr/core/xr_roommesh.c), the triangle budget, the grip transform (the grip's own origin and rotation
land on the origin, a marker where the maths says), the vertex colours (the texture, times AO),
bounding boxes in metres, and that a bad source writes nothing (both hands or neither)."""
import json
import math
import os
import struct
import subprocess
import sys
import tempfile

CONVERTER = sys.argv[1]
failed = 0


def check(cond, what):
    global failed
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


def parse_room(path):
    """roomMeshParse's rules; returns (vertices, indices) or raises AssertionError."""
    d = open(path, 'rb').read()
    assert d[:4] == b'MXR3', 'magic'
    vc, ic, pc, ac = struct.unpack_from('<IIII', d, 4)
    assert 0 < vc < 1 << 20 and ic > 0 and ic % 3 == 0 and pc == 1 and ac == 0
    first, count, atlas = struct.unpack_from('<IIi', d, 20)
    assert (first, count, atlas) == (0, ic, -1), 'one painted part over every index'
    assert struct.unpack_from('<I', d, 32)[0] == 0, 'no channels'
    assert len(d) == 36 + vc * 44 + ic * 4, 'size'
    verts = [struct.unpack_from('<11f', d, 36 + i * 44) for i in range(vc)]
    idx = struct.unpack_from(f'<{ic}I', d, 36 + vc * 44)
    assert max(idx) < vc, 'index range'
    return verts, idx


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


def make_rendermodels(root, ao_level):
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
        make_png(os.path.join(d, name + '_ao.png'), (ao_level,) * 3)


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
        r = run([out, src], MLXR_FRAME_MODEL_TRIANGLES=400)
        check(r.returncode == 0, f'converter failed: {r.stdout}{r.stderr}')
        full = 2 * 30 * 40 + 2 * 8 * 10  # the visible triangles in the source
        rot = rotation(GRIP_ROT)
        for hand in ('left', 'right'):
            path = os.path.join(out, f'frame_controller_{hand}.room')
            check(os.path.isfile(path), f'{hand} written')
            try:
                verts, idx = parse_room(path)
            except (AssertionError, OSError, struct.error) as e:
                check(False, f'{hand} does not parse: {e}')
                continue
            tris = len(idx) // 3
            check(tris < full and tris <= 400 * 1.5, f'{hand}: {tris} triangles for a budget of 400 from {full}')
            check(all(abs(v[0]) < 1e3 and abs(sum(v[3:6]) ) < 1e3 for v in verts), f'{hand}: finite')
            # the body is centred on the grip origin, so its box is centred on 0 and 3 cm a side
            box = bbox([v for v in verts if math.dist(v[:3], (0, 0, 0)) < 0.04])
            for a in range(3):
                check(abs(sum(box[a]) / 2) < 0.006 and 0.04 < box[a][1] - box[a][0] < 0.065, f'{hand}: body box axis {a} {box[a]}')
            # the trigger sphere: model (0, 0, +0.05) from the grip origin -> R^T (0, 0, 0.05)
            want = [sum(rot[j][i] * d for j, d in enumerate((0, 0, 0.05))) for i in range(3)]
            near = [v[:3] for v in verts if math.dist(v[:3], want) < 0.012]
            check(len(near) > 20, f'{hand}: the trigger is where the grip rotation puts it {want}')
            # nothing hidden (the status component sits at (5, 5, 5) m)
            check(max(abs(c) for v in verts for c in v[:3]) < 0.2, f'{hand}: hidden component left out')
            # vertex colours: texture (200, 100, 50) times AO 128/255, in sRGB
            want_rgb = [c * 128 // 255 / 255 for c in (200, 100, 50)]
            mean = [sum(v[8 + k] for v in verts) / len(verts) for k in range(3)]
            check(all(abs(mean[k] - want_rgb[k]) < 0.02 for k in range(3)), f'{hand}: colour {mean} vs {want_rgb}')
            check(all(0.0 <= v[8 + k] <= 1.0 for v in verts for k in range(3)), f'{hand}: colours in range')
            normals = [math.sqrt(sum(c * c for c in v[3:6])) for v in verts]
            check(all(abs(n - 1) < 1e-3 for n in normals), f'{hand}: unit normals')

        # an unsimplified run (budget above the source) keeps every triangle
        out2 = os.path.join(tmp, 'out2')
        r = run([out2, src], MLXR_FRAME_MODEL_TRIANGLES=100000)
        check(r.returncode == 0, 'no-simplify run')
        try:
            check(len(parse_room(os.path.join(out2, 'frame_controller_left.room'))[1]) // 3 == full, 'budget above the source keeps all')
        except (AssertionError, OSError) as e:
            check(False, f'no-simplify output: {e}')

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
            r = run([bout, bad], MLXR_FRAME_MODEL_TRIANGLES=400)
            check(r.returncode != 0 and r.stderr.strip() != '', f'{label}: fails with a message ({r.returncode})')
            left_over = [f for f in (os.listdir(bout) if os.path.isdir(bout) else []) if f.endswith('.room')]
            check(not left_over, f'{label}: nothing written, found {left_over}')

        # --- the real Frame models, when SteamVR is here ------------------------------------------
        real = '/opt/steamvr/drivers/frame_controller/resources/rendermodels'
        if os.path.isdir(real):
            rout = os.path.join(tmp, 'real')
            r = run([rout, real])
            check(r.returncode == 0, f'real models: {r.stdout}{r.stderr}')
            for hand in ('left', 'right'):
                try:
                    verts, idx = parse_room(os.path.join(rout, f'frame_controller_{hand}.room'))
                except (AssertionError, OSError, struct.error) as e:
                    check(False, f'real {hand}: {e}')
                    continue
                tris = len(idx) // 3
                box = bbox(verts)
                print(f'real {hand}: {tris} triangles, {len(verts)} vertices, box {[tuple(round(c, 3) for c in b) for b in box]}')
                check(3000 <= tris <= 6000, f'real {hand}: triangle count {tris}')
                check(len(verts) < 20000, f'real {hand}: vertex count')
                # a handheld controller, in metres around its grip: 5 to 25 cm long, nothing past 20 cm of the grip
                ext = [b[1] - b[0] for b in box]
                check(0.05 < max(ext) < 0.25 and all(-0.2 < c < 0.2 for b in box for c in b), f'real {hand}: size {ext}')
                # handedness: the thumbstick side. The right hand's box leans to -x of the grip, the left's to +x
                lean = (box[0][0] + box[0][1]) / 2
                check((lean < 0) == (hand == 'right'), f'real {hand}: leans the right way ({lean:.4f})')
        else:
            print('no SteamVR here: the real models were not tested')
    print('frame models converter: %s' % ('FAILED %d' % failed if failed else 'ok'))
    return 1 if failed else 0


sys.exit(main())
