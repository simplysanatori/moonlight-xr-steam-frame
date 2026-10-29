#!/usr/bin/env python3
"""Writes the Gun controller model: an original low-poly pistol in the client's model format (MXFM).

The Gun kind of the controller model setting draws this at each hand's OpenXR *aim* pose, so the
beam, the dot and the host cursor all leave the muzzle. It is built here from boxes, prisms and
wedges (no scan, no existing product's shape), as a glTF in memory, and converted by
tools/xr/make_model.py, the same path a user's own model takes, so the file is exactly what that
tool writes: textured MXFM version 2 with a palette image (one flat colour per material), flat
shaded, and a `trigger` component the client animates from /input/trigger.

  python3 tools/xr/make_gun.py assets/models            writes gun_right.model and gun_left.model
  python3 tools/xr/make_gun.py --out gun_left.model --mirror

The pistol is a right hand model, in metres, in the aim pose's axes: +x right, +y up, -z forward.
  - the origin is on the bore axis, above the front of the grip; the aim ray (-z from the origin)
    runs down the barrel and out of the muzzle, 0.135 m in front of it;
  - the slide ends 0.045 m behind it, the grip hangs below it, raked back 22 degrees like the
    Steam Frame controller's handle (tools/xr/make_controller.py leans 23);
  - two sights stand on the slide, parallel to the bore, so the user can sight along it: a post at
    the front and a notch of two blocks at the back, in an accent colour;
  - the trigger is its own component: hinged 0.017 m under the bore at z = -0.031, it swings about
    +x, its foot going back toward the palm by 22 degrees at a full pull (TRIGGER_PIVOT,
    TRIGGER_AXIS, TRIGGER_RANGE below, in the right hand model; --mirror mirrors them);
  - the left hand's file is the right's mirrored in x with the winding flipped.
About 20 cm long and 11 cm tall, a few hundred triangles.
"""
import json
import math
import os
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))

# --- the pistol's measurements, metres (y up, z back) -------------------------------------------

MUZZLE_Z = -0.135       # the front of the barrel: the model's minimum z
SLIDE_FRONT_Z = -0.108
SLIDE_REAR_Z = 0.045
SLIDE_HALF_W = 0.0115
SLIDE_TOP = 0.020
SLIDE_BOTTOM = -0.012
BORE_RADIUS = 0.0055
GRIP_TOP = -0.010
GRIP_LENGTH = 0.085
GRIP_RAKE_DEG = 22.0     # the bottom of the grip sits this far back of vertical
GRIP_FRONT_TOP_Z = 0.010
GRIP_REAR_TOP_Z = 0.042
SIGHT_TOP = 0.0285       # both sights' tops: the sight line is parallel to the bore

TRIGGER_PIVOT = (0.0, -0.017, -0.031)
TRIGGER_AXIS = 'x'
TRIGGER_RANGE = (0.0, -22.0)  # degrees at input 0 and 1; negative swings the foot back (+z)
TRIGGER_INPUT = '/input/trigger'

# Linear base colours: a light steel slide, a dark frame, warm black grips, an orange sight
MATERIALS = {
    'slide': (0.20, 0.215, 0.24),
    'frame': (0.035, 0.037, 0.042),
    'grip': (0.060, 0.050, 0.045),
    'sight': (0.85, 0.22, 0.02),
    'trigger': (0.30, 0.30, 0.32),
}
MATERIAL_ORDER = ('slide', 'frame', 'grip', 'sight', 'trigger')


# --- solids ---------------------------------------------------------------------------------------

def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def octagon(hu, hv, c, cu=0.0, cv=0.0):
    """A rectangle of half sizes hu, hv with its corners cut by c, counter clockwise."""
    return [(cu + hu - c, cv - hv), (cu + hu, cv - hv + c), (cu + hu, cv + hv - c), (cu + hu - c, cv + hv),
            (cu - hu + c, cv + hv), (cu - hu, cv + hv - c), (cu - hu, cv - hv + c), (cu - hu + c, cv - hv)]


def rect(u0, u1, v0, v1):
    return [(u1, v0), (u1, v1), (u0, v1), (u0, v0)]


def circle(r, n):
    return [(r * math.cos(2 * math.pi * i / n), r * math.sin(2 * math.pi * i / n)) for i in range(n)]


def solid(rings):
    """A closed solid through convex rings (lists of 3D points, equally many), the first and last
    capped. Triangles wound outward, whichever way the rings run."""
    tris = []
    n = len(rings[0])
    for r0, r1 in zip(rings, rings[1:]):
        for i in range(n):
            j = (i + 1) % n
            tris.append((r0[i], r0[j], r1[j]))
            tris.append((r0[i], r1[j], r1[i]))
    for i in range(1, n - 1):
        tris.append((rings[0][0], rings[0][i + 1], rings[0][i]))
        tris.append((rings[-1][0], rings[-1][i], rings[-1][i + 1]))
    volume = sum(dot(a, cross(b, c)) for a, b, c in tris)
    if volume < 0:
        tris = [(a, c, b) for a, b, c in tris]
    return tris


def along_z(poly, z0, z1, cx=0.0, cy=0.0):
    return solid([[(cx + u, cy + v, z) for u, v in poly] for z in (z0, z1)])


def along_x(poly, x0, x1):
    """poly in (y, z)."""
    return solid([[(x, y, z) for y, z in poly] for x in (x0, x1)])


def box(x0, x1, y0, y1, z0, z1):
    return along_z(rect(x0, x1, y0, y1), z0, z1)


# --- the parts ----------------------------------------------------------------------------------

def build():
    """{'gun': {material: triangles}, 'trigger': {material: triangles}}."""
    gun = {m: [] for m in MATERIAL_ORDER}
    trigger = {m: [] for m in MATERIAL_ORDER}

    # The slide, a chamfered bar, and the barrel and its muzzle collar poking out of its front
    height = SLIDE_TOP - SLIDE_BOTTOM
    gun['slide'] += along_z(octagon(SLIDE_HALF_W, height / 2, 0.003, 0.0, (SLIDE_TOP + SLIDE_BOTTOM) / 2),
                            SLIDE_REAR_Z, SLIDE_FRONT_Z)
    gun['slide'] += along_z(circle(BORE_RADIUS, 12), SLIDE_FRONT_Z + 0.008, MUZZLE_Z)
    gun['frame'] += along_z(circle(BORE_RADIUS + 0.0013, 12), MUZZLE_Z + 0.007, MUZZLE_Z)

    # Serrations across the back of the slide, an ejection port on its right
    for k in range(6):
        z = 0.012 + 0.0046 * k
        for side in (-1, 1):
            xa, xb = sorted((side * (SLIDE_HALF_W - 0.0004), side * (SLIDE_HALF_W + 0.0006)))
            gun['frame'] += box(xa, xb, -0.006, 0.014, z, z + 0.0024)
    gun['frame'] += box(SLIDE_HALF_W - 0.0004, SLIDE_HALF_W + 0.0006, 0.003, 0.014, -0.040, -0.008)

    # The frame under the slide, the trigger guard, the grip and its base plate
    gun['frame'] += box(-0.0105, 0.0105, -0.030, SLIDE_BOTTOM + 0.001, -0.108, 0.040)
    gun['frame'] += box(-0.0045, 0.0045, -0.058, -0.026, -0.066, -0.060)
    gun['frame'] += box(-0.0045, 0.0045, -0.058, -0.052, -0.066, 0.034)
    shift = GRIP_LENGTH * math.tan(math.radians(GRIP_RAKE_DEG))   # the bottom's shift back

    def grip_ring(y, back, hw, c):
        z0, z1 = GRIP_FRONT_TOP_Z + back, GRIP_REAR_TOP_Z + back
        return [(u, y, v) for u, v in octagon(hw, (z1 - z0) / 2, c, 0.0, (z0 + z1) / 2)]

    gun['grip'] += solid([grip_ring(GRIP_TOP, 0.0, 0.0105, 0.003), grip_ring(GRIP_TOP - GRIP_LENGTH, shift, 0.0125, 0.004)])
    plate = GRIP_TOP - GRIP_LENGTH
    gun['frame'] += solid([grip_ring(plate, shift - 0.001, 0.0132, 0.004),
                           grip_ring(plate - 0.006, shift + 0.002, 0.0132, 0.004)])

    # The sights, standing on the slide: a post at the front, a notch of two blocks at the back
    base = SLIDE_TOP - 0.001
    gun['sight'] += box(-0.0022, 0.0022, base, SIGHT_TOP, SLIDE_FRONT_Z + 0.004, SLIDE_FRONT_Z + 0.010)
    for side in (-1, 1):
        gun['sight'] += box(min(side * 0.0036, side * 0.0092), max(side * 0.0036, side * 0.0092),
                            base, SIGHT_TOP, SLIDE_REAR_Z - 0.011, SLIDE_REAR_Z - 0.004)

    # The trigger, hinged inside the frame: a slanted bar, its foot toward the grip
    profile = [(-0.014, -0.036), (-0.014, -0.027), (-0.046, -0.0215), (-0.046, -0.0285)]
    trigger['trigger'] += along_x(profile, -0.0032, 0.0032)
    return {'gun': gun, 'trigger': trigger}


# --- glTF in memory -------------------------------------------------------------------------------

def gltf_bytes(parts):
    """A GLB of the parts: a node per part, a primitive per material (positions only: the
    converter makes flat normals, and the triangles are wound outward)."""
    blob = bytearray()
    views, accessors, meshes, nodes, used = [], [], [], [], []
    for name in ('gun', 'trigger'):
        prims = []
        for m in MATERIAL_ORDER:
            tris = parts[name][m]
            if not tris:
                continue
            if m not in used:
                used.append(m)
            pts = [p for t in tris for p in t]
            views.append({'buffer': 0, 'byteOffset': len(blob), 'byteLength': len(pts) * 12})
            for p in pts:
                blob += struct.pack('<3f', *p)
            accessors.append({'bufferView': len(views) - 1, 'componentType': 5126, 'count': len(pts), 'type': 'VEC3',
                              'min': [min(p[k] for p in pts) for k in range(3)],
                              'max': [max(p[k] for p in pts) for k in range(3)]})
            prims.append({'attributes': {'POSITION': len(accessors) - 1}, 'material': MATERIAL_ORDER.index(m), 'mode': 4})
        meshes.append({'name': name.capitalize(), 'primitives': prims})
        nodes.append({'name': name.capitalize(), 'mesh': len(meshes) - 1})
    doc = {
        'asset': {'version': '2.0', 'generator': 'tools/xr/make_gun.py'},
        'scene': 0, 'scenes': [{'nodes': [0, 1]}], 'nodes': nodes, 'meshes': meshes,
        'materials': [{'name': m, 'pbrMetallicRoughness': {'baseColorFactor': list(MATERIALS[m]) + [1.0]}}
                      for m in MATERIAL_ORDER],
        'accessors': accessors, 'bufferViews': views, 'buffers': [{'byteLength': len(blob)}],
    }
    text = json.dumps(doc, separators=(',', ':')).encode('utf-8')
    text += b' ' * (-len(text) % 4)
    blob += b'\0' * (-len(blob) % 4)
    total = 12 + 8 + len(text) + 8 + len(blob)
    return (struct.pack('<III', 0x46546C67, 2, total) + struct.pack('<II', len(text), 0x4E4F534A) + text +
            struct.pack('<II', len(blob), 0x004E4942) + bytes(blob))


def write_gun(out, mirror):
    sys.path.insert(0, HERE)
    import make_model
    pivot = ':'.join(f'{c:.6f}' for c in TRIGGER_PIVOT)
    rng = ':'.join(f'{c:g}' for c in TRIGGER_RANGE)
    args = ['--out', out,
            '--component', f'trigger=Trigger,input={TRIGGER_INPUT},motion=rotate,axis={TRIGGER_AXIS},'
                           f'pivot={pivot},range={rng}']
    if mirror:
        args.append('--mirror')
    with tempfile.TemporaryDirectory() as tmp:
        glb = os.path.join(tmp, 'gun.glb')
        with open(glb, 'wb') as f:
            f.write(gltf_bytes(build()))
        make_model.main(['--in', glb] + args)


def main(argv):
    if len(argv) == 1 and not argv[0].startswith('-'):
        os.makedirs(argv[0], exist_ok=True)
        write_gun(os.path.join(argv[0], 'gun_right.model'), False)
        write_gun(os.path.join(argv[0], 'gun_left.model'), True)
    elif len(argv) in (2, 3) and argv[0] == '--out' and argv[2:] in ([], ['--mirror']):
        write_gun(argv[1], bool(argv[2:]))
    else:
        sys.exit('usage: make_gun.py OUTDIR   (writes gun_right.model and gun_left.model)\n'
                 '       make_gun.py --out FILE [--mirror]')


if __name__ == '__main__':
    main(sys.argv[1:])
