#!/usr/bin/env python3
"""Converts SteamVR's Steam Frame controller render models into Moonlight XR Native's model format.

The client can draw a model at each hand's grip (Settings, controller model). It ships one generic
controller; on the Frame this reads the real controllers' models that SteamVR's own frame_controller
driver keeps on every Frame and writes them as textured `.model` files (MXFM, below), which the
client picks up in place of the generic one. Valve's models are only read from your own SteamVR
install on this machine, never redistributed: nothing this writes belongs in git or in a release
(packaging/install.sh runs it at install time).

What is converted, per hand, at full fidelity (as SteamVR draws them):
  - every visible component mesh (body, sticks, buttons, trigger, grips, bumper), every triangle,
    each component its own index range, in OpenXR grip space (the model's `openxr_grip` component),
    the space the client draws a model in; positions, normals, a tangent frame for the normal map
    (per vertex, from the texture coordinates) and the texture coordinates;
  - the textures as SteamVR ships them, at their own size (2048 on the Frame): base colour (sRGB)
    with the ambient occlusion in its alpha, and the tangent space normal map (turned from Valve's
    DirectX green to OpenGL's) with the roughness in its alpha. The metalness map is all zero on the
    Frame's plastic and is left out;
  - each component's motion as SteamVR's model describes it (translate, rotate, joystick: pivot,
    axis, range, the input path that drives it), in grip space, for the client to animate.

Debugging knobs: MLXR_FRAME_MODEL_TRIANGLES=N simplifies each hand toward N triangles with the quadric
collapse from the previous, vertex coloured version (0 or unset: every triangle);
MLXR_FRAME_MODEL_TEXTURE=N scales the textures to N x N (0 or unset: as shipped).

The file (little endian, every field 4 bytes; rooms/room_gpu.c reads it, tests/frame-models checks it):
  header      'MXFM', version 2 (1: the same, without motion type 4), vertexCount, indexCount, componentCount, texWidth, texHeight, flags 0
  components  componentCount records of 208 bytes: name (32 bytes, NUL padded), input path (32
              bytes, NUL padded, '' for none), firstIndex, indexCount, motion type (0 none,
              1 translate, 2 rotate, 3 joystick, 4 d-pad), 0, then 32 floats of motion (MOTION_* below)
  vertices    vertexCount x 12 floats: position 3, normal 3, tangent 4 (w: the bitangent's sign,
              B = w * cross(N, T) runs along +v), uv 2 (v from the top row of the textures down)
  indices     indexCount uint32, into this file's vertices, triangles
  textures    texWidth x texHeight RGBA8 rows from the top: colour (sRGB rgb, linear AO a), then
              surface (normal rgb, OpenGL green, roughness a)

Ported from the owner's Wiicompiled_VR-PLUS-steam-frame/steam-frame/frame-controller-models.py
(GPLv3): the grip transform, the component walk, the motion reading and the quadric simplifier are
that script's. The output format is this client's.

usage: python3 -I frame-controller-models.py OUTDIR [SteamVR rendermodels dir]
writes OUTDIR/frame_controller_left.model and OUTDIR/frame_controller_right.model (and removes the
vertex coloured frame_controller_{left,right}.room an older version wrote there)
Needs Python 3 and GdkPixbuf (python3-gobject), both on SteamOS. No numpy or PIL.
"""
import json
import math
import os
import struct
import sys

RENDERMODELS = '/opt/steamvr/drivers/frame_controller/resources/rendermodels'
TRIANGLES = int(os.environ.get('MLXR_FRAME_MODEL_TRIANGLES', '0') or 0)  # per hand; 0: all of them
TEXTURE = int(os.environ.get('MLXR_FRAME_MODEL_TEXTURE', '0') or 0)      # 0: as shipped
HANDS = ('left', 'right')
VERSION = 2   # 1: no d-pad motion (motion type 4); the client still reads it
VERTEX_FLOATS = 12
COMPONENT_BYTES = 208
NAME_BYTES = 32
MOTION_FLOATS = 32
# Native: version 2 adds the d-pad (4: a joystick's frame, tilt and press, driven by its four
# direction clicks); 'rotate' also covers a button that tilts by value_mapping[1] when clicked
MOTION_TYPES = {'translate': 1, 'rotate': 2, 'joystick': 3, 'dpad': 4}
# The motion floats: what each type reads (all in grip space, metres and degrees)
MOTION_ORIGIN = 0        # 3: rotate pivot, joystick centre
MOTION_AXIS = 3          # 3: translate direction, rotate axis (unit)
MOTION_RANGE = 6         # 2: value_mapping, the offset (m) or angle (deg) at input 0 and at 1
MOTION_PRESSED = 8       # 1: the angle a click forces (pressed_value_override), NaN for none
MOTION_FRAME = 9         # 9: joystick frame (columns: its x, y, z axes in grip space)
MOTION_ROT_X = 18        # 2: joystick_rotation_x, degrees at x = -1 and x = +1
MOTION_ROT_Y = 20        # 2: joystick_rotation_y, degrees at y = -1 and y = +1
MOTION_PRESS = 22        # 3: joystick press_translate, in grip space


# --- small vector maths -------------------------------------------------------------------------

def sub(a, b): return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]
def add(a, b): return [a[0] + b[0], a[1] + b[1], a[2] + b[2]]
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def cross(a, b): return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
def scale(a, s): return [a[0] * s, a[1] * s, a[2] * s]


def norm(a):
    n = math.sqrt(dot(a, a))
    return [a[0] / n, a[1] / n, a[2] / n] if n else [0.0, 0.0, 0.0]


def euler_xyz(degrees):
    """3x3 rotation for SteamVR's rotate_xyz (degrees), applied X then Y then Z; rows of the matrix."""
    x, y, z = (math.radians(d) for d in degrees)
    cx, sx, cy, sy, cz, sz = math.cos(x), math.sin(x), math.cos(y), math.sin(y), math.cos(z), math.sin(z)
    rx = [[1, 0, 0], [0, cx, -sx], [0, sx, cx]]
    ry = [[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]]
    rz = [[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]]
    mul = lambda a, b: [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
    return mul(rz, mul(ry, rx))


# --- input files --------------------------------------------------------------------------------

def read_obj(path):
    """Triangles of an OBJ as lists of (position, uv, normal) corners."""
    vs, vts, vns, tris = [], [], [], []
    for line in open(path):
        parts = line.split()
        if not parts:
            continue
        if parts[0] == 'v':
            vs.append([float(x) for x in parts[1:4]])
        elif parts[0] == 'vt':
            vts.append([float(x) for x in parts[1:3]])
        elif parts[0] == 'vn':
            vns.append([float(x) for x in parts[1:4]])
        elif parts[0] == 'f':
            corners = []
            for ref in parts[1:]:
                i = (ref.split('/') + ['', ''])[:3]
                corners.append((vs[int(i[0]) - 1], vts[int(i[1]) - 1] if i[1] else [0.0, 0.0],
                                vns[int(i[2]) - 1] if i[2] else [0.0, 1.0, 0.0]))
            for k in range(1, len(corners) - 1):  # fan
                tris.append([corners[0], corners[k], corners[k + 1]])
    return tris


def load_pixels(path, size):
    """The image's RGB bytes, tight rows from the top, at size x size (None: as it is). Returns
    (bytes, width, height)."""
    import gi
    gi.require_version('GdkPixbuf', '2.0')
    from gi.repository import GdkPixbuf
    pb = GdkPixbuf.Pixbuf.new_from_file(path)
    if size and (pb.get_width(), pb.get_height()) != size:
        pb = pb.scale_simple(size[0], size[1], GdkPixbuf.InterpType.HYPER)
    w, h, n, stride = pb.get_width(), pb.get_height(), pb.get_n_channels(), pb.get_rowstride()
    px = pb.get_pixels()
    if n == 3 and stride == w * 3:
        return bytes(px[:w * h * 3]), w, h
    rgb = bytearray(w * h * 3)
    for y in range(h):  # padded rows or an alpha channel: drop them
        row = px[y * stride:y * stride + w * n]
        for k in range(3):
            rgb[y * w * 3 + k:(y + 1) * w * 3:3] = row[k::n]
    return bytes(rgb), w, h


def interleave(rgb, a, texels):
    """RGBA bytes from RGB bytes and the first channel of another RGB image (both tight)."""
    out = bytearray(texels * 4)
    out[0::4] = rgb[0::3]
    out[1::4] = rgb[1::3]
    out[2::4] = rgb[2::3]
    out[3::4] = a[0::3]
    return bytes(out)


def textures(directory, prefix):
    """(colour+AO, normal+roughness, width, height): base colour (needed), the rest as flat
    stand-ins when SteamVR has none."""
    first = None if not TEXTURE else (TEXTURE, TEXTURE)
    colour, w, h = load_pixels(os.path.join(directory, f'{prefix}_color.png'), first)
    texels = w * h

    def optional(kind, flat):
        try:
            return load_pixels(os.path.join(directory, f'{prefix}_{kind}.png'), (w, h))[0]
        except Exception:  # GLib.Error for a missing or broken file: drawn without it
            return bytes(flat) * texels

    ao = optional('ao', (255, 255, 255))
    normal = optional('normal', (128, 128, 255))
    rough = optional('rough', (128, 128, 128))
    # Valve's normal maps have DirectX's green (+g down the texture); the client's shader reads
    # OpenGL's (+g up the texture, along +v). Measured: the red channel follows -d(AO)/du and the
    # green +d(AO)/d(row), across both hands.
    flip = bytes(255 - i for i in range(256))
    normal = bytearray(normal)
    normal[1::3] = normal[1::3].translate(flip)
    return interleave(colour, ao, texels), interleave(bytes(normal), rough, texels), w, h


# --- simplification (debugging only) ------------------------------------------------------------

def simplify(tris, target):
    """Quadric error half-edge collapse (Garland and Heckbert) down to about `target` triangles.

    Only original vertices survive, so their texture coordinates and normals carry over. Vertices on
    a texture seam (one position, several UVs), on an open edge or on a non-manifold edge never
    move, which keeps the texture charts and the component's outline intact; the price is that a
    heavily seamed mesh may stop above the target."""
    import heapq
    from collections import defaultdict

    # Weld corners into shared positions; keep each face's own corner attributes.
    ids, P, faces, attrs = {}, [], [], []
    for tri in tris:
        f = []
        for pos, uv, n in tri:
            key = (round(pos[0], 7), round(pos[1], 7), round(pos[2], 7))
            if key not in ids:
                ids[key] = len(P)
                P.append(pos)
            f.append(ids[key])
        if len(set(f)) == 3:
            faces.append(f)
            attrs.append([(uv, n) for _, uv, n in tri])
    if len(faces) <= target:
        return tris

    uv_sets = defaultdict(set)
    edge_count = defaultdict(int)
    vfaces = defaultdict(set)
    for fi, f in enumerate(faces):
        for k in range(3):
            uv = attrs[fi][k][0]
            uv_sets[f[k]].add((round(uv[0], 5), round(uv[1], 5)))
            a, b = f[k], f[(k + 1) % 3]
            edge_count[(min(a, b), max(a, b))] += 1
            vfaces[f[k]].add(fi)
    locked = {p for p, s in uv_sets.items() if len(s) > 1}
    for (a, b), c in edge_count.items():
        if c != 2:
            locked.update((a, b))

    def plane(f):
        a, b, c = P[f[0]], P[f[1]], P[f[2]]
        n = cross(sub(b, a), sub(c, a))
        area = math.sqrt(dot(n, n))
        if area == 0:
            return None, 0.0
        n = scale(n, 1 / area)
        return n + [-dot(n, a)], area

    Q = defaultdict(lambda: [0.0] * 10)
    for f in faces:
        p, w = plane(f)
        if p is None:
            continue
        a, b, c, d = p
        q = [a * a, a * b, a * c, a * d, b * b, b * c, b * d, c * c, c * d, d * d]
        for v in f:
            Q[v] = [x + w * y for x, y in zip(Q[v], q)]

    def error(q, p):
        x, y, z = p
        return (q[0] * x * x + 2 * q[1] * x * y + 2 * q[2] * x * z + 2 * q[3] * x + q[4] * y * y
                + 2 * q[5] * y * z + 2 * q[6] * y + q[7] * z * z + 2 * q[8] * z + q[9])

    alive = [True] * len(faces)
    live = len(faces)
    version = defaultdict(int)
    heap = []

    def neighbours(v):
        return {w for fi in vfaces[v] for w in faces[fi] if w != v}

    def push(u, v):
        if u in locked:
            return
        q = [x + y for x, y in zip(Q[u], Q[v])]
        heapq.heappush(heap, (error(q, P[v]), u, v, version[u], version[v]))

    for (a, b) in edge_count:
        push(a, b)
        push(b, a)

    def face_normal(pts):
        return cross(sub(pts[1], pts[0]), sub(pts[2], pts[0]))

    while live > target and heap:
        _, u, v, vu, vv = heapq.heappop(heap)
        if version[u] != vu or version[v] != vv or u in locked or not vfaces[u] or not vfaces[v]:
            continue
        shared = vfaces[u] & vfaces[v]
        if len(shared) != 2:
            continue
        # Link condition: the edge's two opposite vertices are the only common neighbours.
        if len(neighbours(u) & neighbours(v)) != 2:
            continue
        # No flipped or collapsed triangles around u.
        ok = True
        for fi in vfaces[u] - shared:
            before = [P[w] for w in faces[fi]]
            after = [P[v] if w == u else P[w] for w in faces[fi]]
            nb, na = face_normal(before), face_normal(after)
            la, lb = math.sqrt(dot(na, na)), math.sqrt(dot(nb, nb))
            if la < 1e-12 or lb < 1e-12 or dot(na, nb) / (la * lb) < 0.3:
                ok = False
                break
        if not ok:
            continue
        # v's attributes in the chart the collapsed faces belong to.
        sf = next(iter(shared))
        v_attr = attrs[sf][faces[sf].index(v)]
        for fi in shared:
            alive[fi] = False
            live -= 1
            for w in faces[fi]:
                vfaces[w].discard(fi)
        for fi in list(vfaces[u]):
            k = faces[fi].index(u)
            faces[fi][k] = v
            attrs[fi][k] = v_attr
            vfaces[v].add(fi)
        vfaces[u].clear()
        Q[v] = [x + y for x, y in zip(Q[u], Q[v])]
        version[v] += 1
        ring = neighbours(v)
        for w in ring:
            version[w] += 1
        for w in ring:
            push(v, w)
            push(w, v)

    return [[(P[faces[fi][k]], attrs[fi][k][0], attrs[fi][k][1]) for k in range(3)]
            for fi in range(len(faces)) if alive[fi]]


# --- one hand -----------------------------------------------------------------------------------

def build_vertices(tris, first_vertex):
    """Welds the corners of tris (position, uv, normal, in grip space) into vertices and gives each a
    tangent frame (Lengyel: the triangles' dP/du summed per vertex, made orthogonal to the normal,
    the bitangent's sign from dP/dv). Returns (vertex tuples, indices from first_vertex)."""
    index, pos, nrm, uvs, indices = {}, [], [], [], []
    for tri in tris:
        for p, uv, n in tri:
            key = (round(p[0], 7), round(p[1], 7), round(p[2], 7), round(uv[0], 6), round(uv[1], 6),
                   round(n[0], 5), round(n[1], 5), round(n[2], 5))
            k = index.get(key)
            if k is None:
                k = index[key] = len(pos)
                pos.append(p)
                nrm.append(n)
                uvs.append(uv)
            indices.append(k)
    tan = [[0.0, 0.0, 0.0] for _ in pos]
    bit = [[0.0, 0.0, 0.0] for _ in pos]
    for t in range(0, len(indices), 3):
        i0, i1, i2 = indices[t:t + 3]
        e1, e2 = sub(pos[i1], pos[i0]), sub(pos[i2], pos[i0])
        du1, dv1 = uvs[i1][0] - uvs[i0][0], uvs[i1][1] - uvs[i0][1]
        du2, dv2 = uvs[i2][0] - uvs[i0][0], uvs[i2][1] - uvs[i0][1]
        r = du1 * dv2 - du2 * dv1
        if abs(r) < 1e-12:
            continue
        sdir = scale(sub(scale(e1, dv2), scale(e2, dv1)), 1.0 / r)
        tdir = scale(sub(scale(e2, du1), scale(e1, du2)), 1.0 / r)
        for i in (i0, i1, i2):
            tan[i] = add(tan[i], sdir)
            bit[i] = add(bit[i], tdir)
    verts = []
    for k in range(len(pos)):
        n = nrm[k]
        t = norm(sub(tan[k], scale(n, dot(n, tan[k]))))
        if dot(t, t) < 0.5:  # no UV gradient here: any direction across the normal
            t = norm(cross(n, [0.0, 1.0, 0.0] if abs(n[1]) < 0.9 else [1.0, 0.0, 0.0]))
        w = -1.0 if dot(cross(n, t), bit[k]) < 0.0 else 1.0
        # v turned to run from the textures' top row down, as the image rows are uploaded
        verts.append((*pos[k], *n, *t, w, uvs[k][0], 1.0 - uvs[k][1]))
    return verts, [first_vertex + i for i in indices]


def motion_block(motion, to_grip, dir_to_grip):
    """(type, input path, MOTION_FLOATS floats) of a component's SteamVR motion, in grip space."""
    f = [0.0] * MOTION_FLOATS
    f[MOTION_PRESSED] = float('nan')
    if not motion or motion.get('type') not in MOTION_TYPES:
        return 0, '', f
    kind = motion['type']
    path = (motion.get('component_path') or motion.get('joystick_path') or motion.get('trigger_path')
            or motion.get('pressed_path') or motion.get('dpad_up_path') or '')
    rng = motion.get('value_mapping', [0.0, 0.0])
    f[MOTION_RANGE:MOTION_RANGE + 2] = [float(rng[0]), float(rng[1])]
    if 'pressed_value_override' in motion:
        f[MOTION_PRESSED] = float(motion['pressed_value_override'])
    if kind in ('translate', 'rotate'):
        f[MOTION_AXIS:MOTION_AXIS + 3] = norm(dir_to_grip(motion['axis']))
    if kind == 'rotate':
        f[MOTION_ORIGIN:MOTION_ORIGIN + 3] = to_grip(motion['pivot'])
    if kind in ('joystick', 'dpad'):
        f[MOTION_ORIGIN:MOTION_ORIGIN + 3] = to_grip(motion['center'])
        frame = euler_xyz(motion.get('rotate_xyz', [0, 0, 0]))
        for c in range(3):  # the frame's axis c, model space, then grip space
            f[MOTION_FRAME + 3 * c:MOTION_FRAME + 3 * c + 3] = dir_to_grip([frame[r][c] for r in range(3)])
        f[MOTION_ROT_X:MOTION_ROT_X + 2] = [float(x) for x in motion.get('joystick_rotation_x', [0, 0])]
        f[MOTION_ROT_Y:MOTION_ROT_Y + 2] = [float(x) for x in motion.get('joystick_rotation_y', [0, 0])]
        press = motion.get('press_translate', [0.0, 0.0, 0.0])
        f[MOTION_PRESS:MOTION_PRESS + 3] = dir_to_grip([sum(frame[r][c] * press[c] for c in range(3)) for r in range(3)])
    return MOTION_TYPES[kind], path, f


def convert(hand, rendermodels):
    """One hand's model in OpenXR grip space: a dict of components, vertices, indices, textures."""
    name = f'frame_controller_{hand}'
    directory = os.path.join(rendermodels, name)
    components = json.load(open(os.path.join(directory, name + '.json')))['components']

    grip = components.get('openxr_grip') or components.get('handgrip')
    grip_origin = grip['component_local']['origin']
    grip_rot = euler_xyz(grip['component_local'].get('rotate_xyz', [0, 0, 0]))
    to_grip = lambda p: [sum(grip_rot[j][i] * (p[j] - grip_origin[j]) for j in range(3)) for i in range(3)]
    dir_to_grip = lambda d: [sum(grip_rot[j][i] * d[j] for j in range(3)) for i in range(3)]

    meshes = {}
    for key, c in components.items():
        if 'filename' in c and c.get('visibility', {}).get('default', True):
            meshes[key] = read_obj(os.path.join(directory, c['filename']))
    if not meshes:
        raise ValueError(f'{name}: no visible component meshes')
    full = sum(len(t) for t in meshes.values())

    # Debugging only: the budget shared between components by size with a floor so small buttons
    # keep their shape. Largest first: the body's seams can stop it above its share.
    if TRIANGLES > 0:
        remaining, left_to_do = TRIANGLES, full
        for key in sorted(meshes, key=lambda k: -len(meshes[k])):
            n = len(meshes[key])
            meshes[key] = simplify(meshes[key], max(min(n, 60), round(n * remaining / left_to_do)))
            remaining -= len(meshes[key])
            left_to_do -= n

    # The body first, then the moving parts by name, each its own range
    order = sorted(meshes, key=lambda k: (k != 'body', k))
    verts, indices, comps = [], [], []
    for key in order:
        tris = [[(to_grip(p), uv, norm(dir_to_grip(n))) for p, uv, n in tri] for tri in meshes[key]]
        v, i = build_vertices(tris, len(verts))
        kind, path, motion = motion_block(components[key].get('motion'), to_grip, dir_to_grip)
        comps.append({'name': key, 'path': path, 'first': len(indices), 'count': len(i), 'type': kind,
                      'motion': motion})
        verts += v
        indices += i
    if len(verts) >= 1 << 22:
        raise ValueError(f'{name}: {len(verts)} vertices, more than the client reads')
    colour, surface, tw, th = textures(directory, name)
    print(f'{hand}: {len(indices) // 3} triangles of {full}, {len(verts)} vertices, {len(comps)} components, '
          f'textures {tw}x{th}')
    return {'components': comps, 'vertices': verts, 'indices': indices, 'colour': colour, 'surface': surface,
            'width': tw, 'height': th}


def pack_name(s):
    b = s.encode('utf-8')[:NAME_BYTES - 1]
    return b + b'\0' * (NAME_BYTES - len(b))


def write_model(path, m):
    comps, verts, indices = m['components'], m['vertices'], m['indices']
    tmp = path + '.tmp'
    with open(tmp, 'wb') as f:
        f.write(struct.pack('<4s7I', b'MXFM', VERSION, len(verts), len(indices), len(comps), m['width'],
                            m['height'], 0))
        for c in comps:
            f.write(pack_name(c['name']) + pack_name(c['path']))
            f.write(struct.pack('<4I', c['first'], c['count'], c['type'], 0))
            f.write(struct.pack(f'<{MOTION_FLOATS}f', *c['motion']))
        f.write(struct.pack(f'<{len(verts) * VERTEX_FLOATS}f', *[x for v in verts for x in v]))
        f.write(struct.pack(f'<{len(indices)}I', *indices))
        f.write(m['colour'])
        f.write(m['surface'])
    os.replace(tmp, path)


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    out = sys.argv[1]
    src = sys.argv[2] if len(sys.argv) > 2 else RENDERMODELS
    if not os.path.isdir(src):
        sys.exit(f'{src}: no Steam Frame controller models here (is SteamVR installed?)')
    try:
        import gi
        gi.require_version('GdkPixbuf', '2.0')
        from gi.repository import GdkPixbuf  # noqa: F401
    except (ImportError, ValueError) as e:
        sys.exit(f'GdkPixbuf is not available to Python ({e}): python3-gobject is needed')
    try:
        models = {hand: convert(hand, src) for hand in HANDS}
    except Exception as e:  # OSError, ValueError, KeyError, IndexError, GLib.Error (a bad texture)
        sys.exit(f'could not convert the Steam Frame controller models from {src}: {e!r}')
    # Both hands or neither: the client wants a pair
    os.makedirs(out, exist_ok=True)
    for hand in HANDS:
        write_model(os.path.join(out, f'frame_controller_{hand}.model'), models[hand])
    for hand in HANDS:  # the vertex coloured pair an older converter wrote
        old = os.path.join(out, f'frame_controller_{hand}.room')
        if os.path.exists(old):
            os.remove(old)


if __name__ == '__main__':
    main()
