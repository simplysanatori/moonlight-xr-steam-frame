#!/usr/bin/env python3
"""Converts SteamVR's Steam Frame controller render models into Moonlight XR Native's model format.

The client can draw a model at each hand's grip (Settings, controller model). It ships one generic
controller; on the Frame this reads the real controllers' models that SteamVR's own frame_controller
driver keeps on every Frame and writes them as `.room` meshes (MXR3, painted from vertex colours),
which the client picks up in place of the generic one. Valve's models are only read from your own
SteamVR install on this machine, never redistributed: nothing this writes belongs in git or in a
release (packaging/install.sh runs it at install time).

What is converted, per hand:
  - every visible component mesh (body, sticks, buttons, trigger, grips, bumper), triangulated, in
    OpenXR grip space (the model's `openxr_grip` component), the space the client draws a model in;
  - simplified toward 4000 triangles per hand (MLXR_FRAME_MODEL_TRIANGLES; texture seams leave the
    right hand a little above that);
  - the base colour texture with its ambient occlusion baked in (times MLXR_FRAME_MODEL_GAIN in linear
    light, default 1), then carried onto the vertices:
    each vertex takes the average of that texture under its own hat function, over the triangles
    around it in its own texture chart, so colours do not bleed across seams.

Ported from the owner's Wiicompiled_VR-PLUS-steam-frame/steam-frame/frame-controller-models.py
(GPLv3): the grip transform, the component walk and the quadric simplifier are that script's. The
output format and the vertex colours are this client's; the controller motions are not converted.

usage: python3 -I frame-controller-models.py OUTDIR [SteamVR rendermodels dir]
writes OUTDIR/frame_controller_left.room and OUTDIR/frame_controller_right.room
Needs Python 3 and GdkPixbuf (python3-gobject), both on SteamOS. No numpy or PIL.
"""
import json
import math
import os
import struct
import sys

RENDERMODELS = '/opt/steamvr/drivers/frame_controller/resources/rendermodels'
TEXTURE_SIZE = 1024
TRIANGLES = int(os.environ.get('MLXR_FRAME_MODEL_TRIANGLES', '4000'))  # per hand
# The Frame's plastic is near black (about sRGB 10 of 255) and the client lights a vertex colour with
# one diffuse light and no gloss, so it reads as a silhouette in a dark room. A gain above 1 lifts
# it toward what the glossy original looks like; 1 is the texture as SteamVR ships it.
GAIN = float(os.environ.get('MLXR_FRAME_MODEL_GAIN', '1'))
HANDS = ('left', 'right')


# --- small vector maths -------------------------------------------------------------------------

def sub(a, b): return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]
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


def load_pixbuf(path):
    import gi
    gi.require_version('GdkPixbuf', '2.0')
    from gi.repository import GdkPixbuf
    pb = GdkPixbuf.Pixbuf.new_from_file(path)
    return pb.scale_simple(TEXTURE_SIZE, TEXTURE_SIZE, GdkPixbuf.InterpType.HYPER)


SRGB_TO_LINEAR = [((i / 255.0) / 12.92 if i / 255.0 <= 0.04045 else ((i / 255.0 + 0.055) / 1.055) ** 2.4)
                  for i in range(256)]


def linear_to_srgb(c):
    c = min(1.0, max(0.0, c))
    return c * 12.92 if c <= 0.0031308 else 1.055 * c ** (1 / 2.4) - 0.055


class Texture:
    """Base colour times ambient occlusion (both from SteamVR's files), sampled bilinearly in linear
    light. Texture rows run top down; an OBJ's V runs bottom up."""

    def __init__(self, directory, prefix):
        colour = load_pixbuf(os.path.join(directory, f'{prefix}_color.png'))
        try:
            ao = load_pixbuf(os.path.join(directory, f'{prefix}_ao.png'))
        except Exception:
            ao = None  # a model without AO is drawn as its base colour
        n = TEXTURE_SIZE
        cp, cs, cc = colour.get_pixels(), colour.get_rowstride(), colour.get_n_channels()
        ap, as_, ac = (ao.get_pixels(), ao.get_rowstride(), ao.get_n_channels()) if ao else (None, 0, 0)
        lin = SRGB_TO_LINEAR
        # Linear RGB per texel, the AO multiplied into the sRGB bytes as the fork does
        self.texels = []
        for y in range(n):
            crow, arow = y * cs, y * as_
            row = []
            for x in range(n):
                c = crow + x * cc
                a = ap[arow + x * ac] if ap is not None else 255
                row.append((lin[cp[c] * a // 255], lin[cp[c + 1] * a // 255], lin[cp[c + 2] * a // 255]))
            self.texels.append(row)

    def sample(self, u, v):
        n = TEXTURE_SIZE
        x = (u % 1.0) * n - 0.5
        y = ((1.0 - v) % 1.0) * n - 0.5
        x0, y0 = math.floor(x), math.floor(y)
        fx, fy = x - x0, y - y0
        x0, x1 = x0 % n, (x0 + 1) % n
        y0, y1 = y0 % n, (y0 + 1) % n
        t = self.texels
        a, b, c, d = t[y0][x0], t[y0][x1], t[y1][x0], t[y1][x1]
        w = ((1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy)
        return tuple(a[i] * w[0] + b[i] * w[1] + c[i] * w[2] + d[i] * w[3] for i in range(3))


# --- simplification -----------------------------------------------------------------------------

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

def build_vertices(tris, tex):
    """Welds the corners of tris (position, uv, normal, in grip space) into vertices and paints each
    with the texture under its hat function: the average over the triangles around it, in its own
    chart (the key holds the UV, so a seam keeps its two sides apart), of the texture weighted by
    the triangle's area and by how much of the point is this vertex's barycentric share."""
    index, verts, wsum, csum, indices = {}, [], [], [], []
    for tri in tris:
        ids = []
        for pos, uv, n in tri:
            key = (round(pos[0], 6), round(pos[1], 6), round(pos[2], 6), round(uv[0], 5), round(uv[1], 5),
                   round(n[0], 4), round(n[1], 4), round(n[2], 4))
            if key not in index:
                index[key] = len(verts)
                verts.append((pos, n, uv))
                wsum.append(0.0)
                csum.append([0.0, 0.0, 0.0])
            ids.append(index[key])
        indices.extend(ids)
        a, b, c = (t[0] for t in tri)
        nrm = cross(sub(b, a), sub(c, a))
        area = 0.5 * math.sqrt(dot(nrm, nrm))
        if area <= 0.0:
            continue
        # Sub-triangle grid sized to how many texels the triangle covers, 2..8 a side
        uvs = [t[1] for t in tri]
        texels = 0.5 * abs((uvs[1][0] - uvs[0][0]) * (uvs[2][1] - uvs[0][1])
                           - (uvs[2][0] - uvs[0][0]) * (uvs[1][1] - uvs[0][1])) * TEXTURE_SIZE ** 2
        n = max(2, min(8, int(math.sqrt(texels) / 6) + 1))
        weight = area / (n * n)
        for i in range(n):
            for j in range(n - i):
                # up triangle (i, j) and, but for the row's last, the down triangle between
                for cb in ((i + 1 / 3.0, j + 1 / 3.0),) + (((i + 2 / 3.0, j + 2 / 3.0),) if j < n - i - 1 else ()):
                    b1, b2 = cb[0] / n, cb[1] / n
                    b0 = 1.0 - b1 - b2
                    u = b0 * uvs[0][0] + b1 * uvs[1][0] + b2 * uvs[2][0]
                    v = b0 * uvs[0][1] + b1 * uvs[1][1] + b2 * uvs[2][1]
                    r, g, bl = tex.sample(u, v)
                    for corner, share in ((0, b0), (1, b1), (2, b2)):
                        w = weight * share
                        k = ids[corner]
                        wsum[k] += w
                        csum[k][0] += w * r
                        csum[k][1] += w * g
                        csum[k][2] += w * bl
    colours = []
    for k in range(len(verts)):
        w = wsum[k]
        colours.append([linear_to_srgb(GAIN * c / w) for c in csum[k]] if w > 0 else [0.5, 0.5, 0.5])
    return verts, colours, indices


def write_room(path, verts, colours, indices):
    """MXR3 (rooms/REPORT.md, xr/core/xr_roommesh.h): one part painted from its vertex colours."""
    data = bytearray(struct.pack('<4sIIII', b'MXR3', len(verts), len(indices), 1, 0))
    data += struct.pack('<IIi', 0, len(indices), -1)
    data += struct.pack('<I', 0)  # no channels
    for (pos, n, uv), rgb in zip(verts, colours):
        data += struct.pack('<11f', *pos, *n, uv[0], uv[1], *rgb)
    data += struct.pack(f'<{len(indices)}I', *indices)
    tmp = path + '.tmp'
    with open(tmp, 'wb') as f:
        f.write(data)
    os.replace(tmp, path)


def convert(hand, rendermodels):
    """Returns (verts, colours, indices) of one hand's model in OpenXR grip space."""
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

    # The triangle budget shared between components by size with a floor so small buttons keep their
    # shape. Largest first: the body's seams can stop it above its share, and the rest absorb that.
    remaining, left_to_do = TRIANGLES, sum(len(t) for t in meshes.values())
    for key in sorted(meshes, key=lambda k: -len(meshes[k])):
        n = len(meshes[key])
        meshes[key] = simplify(meshes[key], max(min(n, 60), round(n * remaining / left_to_do)))
        remaining -= len(meshes[key])
        left_to_do -= n

    tex = Texture(directory, name)
    tris = [[(to_grip(pos), uv, norm(dir_to_grip(nrm))) for pos, uv, nrm in tri]
            for key in meshes for tri in meshes[key]]
    verts, colours, indices = build_vertices(tris, tex)
    if len(verts) >= 1 << 20:
        raise ValueError(f'{name}: {len(verts)} vertices, more than the client reads')
    print(f'{hand}: {len(indices) // 3} triangles, {len(verts)} vertices from {len(meshes)} components')
    return verts, colours, indices


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
    except (OSError, ValueError, KeyError, IndexError) as e:
        sys.exit(f'could not convert the Steam Frame controller models from {src}: {e!r}')
    # Both hands or neither: the client wants a pair
    os.makedirs(out, exist_ok=True)
    for hand in HANDS:
        write_room(os.path.join(out, f'frame_controller_{hand}.room'), *models[hand])


if __name__ == '__main__':
    main()
