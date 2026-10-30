#!/usr/bin/env python3
"""Converts the Gun controller model, a Glock 17 with a weapon light, into the client's MXFM files.

The Gun kind of the controller model setting draws assets/models/gun_right.model and
gun_left.model at each hand's OpenXR *aim* pose, so the beam, the dot and the host cursor all leave
the muzzle. They are made by this script from "G17 Gen 5 Handgun + Olight - NSW Police Force" by
8sianDude (Sketchfab user haoliu95), CC BY 4.0,
https://sketchfab.com/3d-models/g17-gen-5-handgun-olight-nsw-police-force-0200c248eba14385ad2bee8d4dc4e586
(credited in assets/licenses/README.md; what is changed from it is listed there).

  python3 tools/xr/make_gun.py --in g17.glb assets/models          writes gun_right.model, gun_left.model
  python3 tools/xr/make_gun.py --in g17.glb --inspect               prints what the profile needs

The model is read by tools/xr/make_model.py (glTF or GLB, node transforms applied) and written by it,
so the files are what a user's own model gives: textured MXFM version 2 with a palette image.
Steps, with the numbers in PROFILE (a JSON file of the same keys replaces them for another model: --profile):
  1. what is not the pistol is left out: the nodes named in `drop_nodes` (with everything under
     them), all but the largest connected pieces of the nodes in `keep_pieces` ({node: how many}),
     then, if `keep_box` is given, every connected piece whose middle lies outside it (in the
     turned model, the model's own units);
  2. turned so its barrel points along -z and its top along +y (`forward`, `up`: the model's own
     axes after its node transforms), then scaled so it is `length` metres long;
  3. moved so the bore (`bore`: the barrel's axis, x and y, in the turned and scaled model) is the z
     axis: the aim ray runs down the barrel and out of the muzzle;
  4. slid along z until the grip's centre line (`grip`: a point of it, y and z) crosses the line
     of a hand held pistol's grip at that height: raked back GRIP_RAKE_DEG, its middle at
     PISTOL_GRIP, where the Steam Frame controller's handle is (the numbers of the original pistol
     this model replaced);
  5. the trigger made its own component (`trigger_node`, a node name, or else `trigger_box`: every
     connected piece lying wholly inside that box of the turned and scaled model), hinged at the top
     front of its bounds (or `trigger_pivot`), swung about +x by /input/trigger, its foot going back
     toward the palm;
  6. each triangle coloured from its material (the base colour texture sampled across it, or the
     flat colour; `node_colours` {node: linear rgb} sets a node's), the colours reduced to
     `colours` (linear light), one palette texel each;
  7. if it has more than `triangles` (both parts), the body is simplified with quadric edge collapse
     (needs numpy and pyfqmr: pip install numpy pyfqmr), every new triangle taking the colour of the
     nearest old one, its normals smooth with creases over CREASE_DEG kept sharp;
  8. tools/xr/make_model.py writes the right hand's file and, with --mirror, the left's.
JPEG textures need Pillow (pip install pillow); PNG ones do not.
"""
import argparse
import json
import math
import os
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import make_model  # noqa: E402

# --- where a hand held pistol goes in the aim pose (metres) -----------------------------------------

PISTOL_GRIP = (-0.0525, 0.043)   # (y, z): the middle of the grip's centre line
GRIP_RAKE_DEG = 22.0             # the grip's lean back from vertical, like the Frame controller's handle
TRIGGER_AXIS = 'x'
TRIGGER_RANGE = (0.0, -22.0)     # degrees at input 0 and 1; negative swings the foot back (+z)
TRIGGER_INPUT = '/input/trigger'
CREASE_DEG = 50.0

# The G17 model's numbers (see the top of the file). The scene also holds two spare magazines behind
# the pistol (dropped) and the one in its grip (kept, without the cartridges inside it, which cannot
# be seen); in the turned model, metres once scaled: the bore at y 0.1244 (the barrel's front ring,
# 9 mm across), the grip's centre line through y 0.053 z 0.0632 (raked back 25 degrees), the trigger
# and its safety blade x +-0.0035, y 0.073..0.105, z 0..0.020
PROFILE = {
    'forward': '+z',
    'up': '+y',
    'drop_nodes': ['mag_full.001', 'mag_empty'],
    'keep_box': None,
    'keep_pieces': {'mag_full': 3},          # its body, inner wall and base plate
    'length': 0.2135,                        # the light's front to the frame's back: the slide is 0.186 m
    'bore': [0.0, 0.1244],
    'grip': [0.053, 0.0632],
    'trigger_node': None,
    'trigger_box': [[-0.005, 0.070, -0.002], [0.005, 0.106, 0.022]],
    'trigger_pivot': None,
    'node_colours': {'body_low_olight_pl2_valkyrie_lens_0': [0.75, 0.75, 0.70]},   # the light's lens, pale
    'colours': 16,
    'triangles': 30000,
}


# What --profile starts from
NEUTRAL = {'forward': '-z', 'up': '+y', 'drop_nodes': [], 'keep_box': None, 'keep_pieces': None,
           'trigger_node': None, 'trigger_box': None, 'trigger_pivot': None, 'node_colours': None,
           'colours': 16, 'triangles': 30000}


class GunError(Exception):
    pass


# --- small vector maths -----------------------------------------------------------------------------

def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


AXES = {'+x': (1, 0, 0), '-x': (-1, 0, 0), '+y': (0, 1, 0), '-y': (0, -1, 0), '+z': (0, 0, 1), '-z': (0, 0, -1)}


def rotation(forward, up):
    """Rows taking the model's axes to the aim pose's: forward to -z, up to +y."""
    if forward not in AXES or up not in AXES:
        raise GunError('forward and up are one of +x -x +y -y +z -z')
    f, u = AXES[forward], AXES[up]
    if dot(f, u) != 0:
        raise GunError('forward and up must be different axes')
    return (cross(f, u), u, (-f[0], -f[1], -f[2]))


def srgb_to_linear(c):
    c /= 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def area(t):
    n = cross(sub(t[1], t[0]), sub(t[2], t[0]))
    return 0.5 * math.sqrt(dot(n, n))


# --- reading ---------------------------------------------------------------------------------------

def decode_image(data, what):
    """(rgb bytes, width, height): by Pillow when it is installed (fast), else PNG by make_model's
    reader (plain Python: a minute or so for a 4096 x 4096 texture)."""
    try:
        from PIL import Image
    except ImportError:
        Image = None
    if Image is not None:
        import io
        im = Image.open(io.BytesIO(data)).convert('RGB')
        return im.tobytes(), im.width, im.height
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise GunError(f'{what} is not a PNG: install Pillow (pip install pillow) to read it')
    return make_model.read_png(data, what)


def triangle_colours(tris, image_loader, overrides):
    """A linear rgb per triangle: the profile's colour for its node, its material's flat colour, or
    its texture's average over five points of it."""
    images, out = {}, []
    for t in tris:
        given = next((overrides[n] for n in t.names if n in overrides), None)
        if given is not None:
            out.append(tuple(float(c) for c in given))
            continue
        m = t.material
        if m is None or m[0] == 'color':
            out.append(tuple(m[1]) if m else (1.0, 1.0, 1.0))
            continue
        if m[1] not in images:
            images[m[1]] = decode_image(image_loader(m[1]), f'image {m[1]}')
        rgb, w, h = images[m[1]]
        uvs = [c[1] for c in t.corners]
        if any(uv is None for uv in uvs):
            out.append((0.5, 0.5, 0.5))
            continue
        cu = sum(uv[0] for uv in uvs) / 3
        cv = sum(uv[1] for uv in uvs) / 3
        pts = [(cu, cv), (cu, cv)] + [((cu + uv[0]) / 2, (cv + uv[1]) / 2) for uv in uvs]
        acc = [0.0, 0.0, 0.0]
        for u, v in pts:
            x = int((u % 1.0) * w) % w
            y = int((v % 1.0) * h) % h
            k = (y * w + x) * 3
            for c in range(3):
                acc[c] += srgb_to_linear(rgb[k + c])
        out.append(tuple(a / len(pts) for a in acc))
    return out


# --- shaping ----------------------------------------------------------------------------------------

def pieces(tris_pts):
    """Connected pieces (triangles sharing a corner position) as lists of triangle indices."""
    parent = list(range(len(tris_pts)))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    owner = {}
    for i, t in enumerate(tris_pts):
        for p in t:
            j = owner.setdefault((round(p[0], 6), round(p[1], 6), round(p[2], 6)), i)
            if j != i:
                a, b = find(i), find(j)
                if a != b:
                    parent[a] = b
    groups = {}
    for i in range(len(tris_pts)):
        groups.setdefault(find(i), []).append(i)
    return list(groups.values())


def pistol_only(tris, prof, rows):
    """The triangles that are the pistol: named nodes dropped, then pieces outside keep_box."""
    drop = set(prof.get('drop_nodes') or [])
    kept = [t for t in tris if not drop.intersection(t.names)]
    for node, count in (prof.get('keep_pieces') or {}).items():
        mine = [i for i, t in enumerate(kept) if node in t.names]
        if not mine:
            raise GunError(f'keep_pieces: no node named {node!r}')
        ps = sorted(pieces([[c[0] for c in kept[i].corners] for i in mine]), key=len, reverse=True)
        gone = {mine[i] for piece in ps[int(count):] for i in piece}
        kept = [t for i, t in enumerate(kept) if i not in gone]
    box = prof.get('keep_box')
    if box:
        turned = [[tuple(dot(r, c[0]) for r in rows) for c in t.corners] for t in kept]
        keep = []
        for piece in pieces(turned):
            pts = [p for i in piece for p in turned[i]]
            mid = [(min(p[k] for p in pts) + max(p[k] for p in pts)) / 2 for k in range(3)]
            if all(box[0][k] <= mid[k] <= box[1][k] for k in range(3)):
                keep += piece
        kept = [kept[i] for i in sorted(keep)]
    if not kept:
        raise GunError('nothing is left of the model after drop_nodes and keep_box')
    return kept


def placed(tris, prof, rows):
    """Every triangle's corners turned, scaled and moved into the aim pose, the scale and the
    offset (added after scaling)."""
    turned = [[tuple(dot(r, c[0]) for r in rows) for c in t.corners] for t in tris]
    zs = [p[2] for t in turned for p in t]
    extent = max(zs) - min(zs)
    if not extent > 0:
        raise GunError('the model has no length along its forward axis')
    s = prof['length'] / extent
    bx, by = prof['bore']
    gy, gz = prof['grip']
    # the grip point onto the pistol's grip line at its height (once the bore is at y = 0)
    y = gy - by
    want_z = PISTOL_GRIP[1] - (y - PISTOL_GRIP[0]) * math.tan(math.radians(GRIP_RAKE_DEG))
    off = (-bx, -by, want_z - gz)
    out = [[(p[0] * s + off[0], p[1] * s + off[1], p[2] * s + off[2]) for p in t] for t in turned]
    return out, s, off


def split_trigger(tris, pts, prof, off):
    """The indices of the trigger's triangles: a node's, or the pieces wholly inside the box (given
    in the turned and scaled model, so moved with the rest here)."""
    if prof.get('trigger_node'):
        name = prof['trigger_node']
        idx = [i for i, t in enumerate(tris) if name in t.names]
        if not idx:
            raise GunError(f'no node named {name!r}')
        return idx
    box = prof.get('trigger_box')
    if not box:
        raise GunError('the profile names no trigger (trigger_node or trigger_box)')
    lo = [box[0][k] + off[k] for k in range(3)]
    hi = [box[1][k] + off[k] for k in range(3)]
    idx = []
    for piece in pieces(pts):
        if all(lo[k] <= p[k] <= hi[k] for i in piece for p in pts[i] for k in range(3)):
            idx += piece
    if not idx:
        raise GunError('nothing lies wholly inside trigger_box')
    return sorted(idx)


def quantise(colours, weights, n):
    """Up to n colours for the list (k-means in linear rgb, area weighted, seeded far apart).
    Returns (palette, index per colour)."""
    rk = lambda c: (round(c[0], 3), round(c[1], 3), round(c[2], 3))
    distinct = {}
    for c, w in zip(colours, weights):
        k = rk(c)
        distinct[k] = distinct.get(k, 0.0) + w
    keys = sorted(distinct)
    if len(keys) <= n:
        where = {k: i for i, k in enumerate(keys)}
        return keys, [where[rk(c)] for c in colours]
    d2 = lambda a, b: (a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2
    centres = [max(keys, key=lambda k: distinct[k])]
    near = [d2(k, centres[0]) for k in keys]
    while len(centres) < n:
        i = max(range(len(keys)), key=lambda j: near[j] * math.sqrt(distinct[keys[j]]))
        centres.append(keys[i])
        near = [min(a, d2(k, keys[i])) for a, k in zip(near, keys)]
    for _ in range(12):
        acc = [[0.0, 0.0, 0.0, 0.0] for _ in centres]
        for k in keys:
            a = acc[min(range(len(centres)), key=lambda c: d2(k, centres[c]))]
            w = distinct[k]
            a[0] += k[0] * w
            a[1] += k[1] * w
            a[2] += k[2] * w
            a[3] += w
        centres = [(a[0] / a[3], a[1] / a[3], a[2] / a[3]) if a[3] > 0 else c for a, c in zip(acc, centres)]
    label = {k: min(range(len(centres)), key=lambda c: d2(k, centres[c])) for k in keys}
    return centres, [label[rk(c)] for c in colours]


def own_normals(pts, tris, rows):
    """The model's own corner normals turned with it (flat ones where it has none)."""
    out = []
    for t, src in zip(pts, tris):
        face = cross(sub(t[1], t[0]), sub(t[2], t[0]))
        ln = math.sqrt(dot(face, face)) or 1.0
        face = (face[0] / ln, face[1] / ln, face[2] / ln)
        corners = []
        for c in src.corners:
            if c[2] is None:
                corners.append(face)
                continue
            v = tuple(dot(r, c[2]) for r in rows)
            lv = math.sqrt(dot(v, v)) or 1.0
            corners.append((v[0] / lv, v[1] / lv, v[2] / lv))
        out.append(corners)
    return out


# --- simplifying (numpy and pyfqmr, only when over the budget) --------------------------------------

def simplify(pts, labels, target):
    """pts: triangles (3 points each), labels: a palette index each. Returns the simplified
    (triangles, labels, corner normals)."""
    try:
        import numpy as np
        import pyfqmr
    except ImportError:
        raise GunError(f'the model has {len(pts)} triangles, over the budget of {target}: simplifying '
                       'needs numpy and pyfqmr (pip install numpy pyfqmr)')
    t = np.array(pts, dtype=np.float64)
    flat = t.reshape(-1, 3)
    uniq, inv = np.unique(np.round(flat * 1e6).astype(np.int64), axis=0, return_inverse=True)
    verts = np.zeros((len(uniq), 3))
    verts[inv.reshape(-1)] = flat
    s = pyfqmr.Simplify()
    s.setMesh(verts, inv.reshape(-1, 3).astype(np.int32))
    s.simplify_mesh(target_count=int(target), aggressiveness=7, preserve_border=True, verbose=False)
    v, f, _ = s.getMesh()
    v, f = np.asarray(v, dtype=np.float64), np.asarray(f, dtype=np.int64)
    f = f[(f[:, 0] != f[:, 1]) & (f[:, 1] != f[:, 2]) & (f[:, 0] != f[:, 2])]
    p = v[f]
    keep = np.linalg.norm(np.cross(p[:, 1] - p[:, 0], p[:, 2] - p[:, 0]), axis=1) > 1e-14
    f, p = f[keep], p[keep]
    # each new triangle takes the colour of the old triangle nearest it that faces the same way
    unit = lambda q: q / np.maximum(np.linalg.norm(q, axis=1, keepdims=True), 1e-20)
    old_c, new_c = t.mean(axis=1), p.mean(axis=1)
    old_n = unit(np.cross(t[:, 1] - t[:, 0], t[:, 2] - t[:, 0]))
    new_n = unit(np.cross(p[:, 1] - p[:, 0], p[:, 2] - p[:, 0]))
    scale = float(np.ptp(t.reshape(-1, 3), axis=0).max())
    lab = np.array(labels)
    out = np.zeros(len(f), dtype=np.int64)
    for c in range(0, len(f), 128):
        d = ((new_c[c:c + 128, None, :] - old_c[None, :, :]) ** 2).sum(axis=2)
        d += (1.0 - new_n[c:c + 128] @ old_n.T) * (0.01 * scale) ** 2
        out[c:c + 128] = lab[np.argmin(d, axis=1)]
    return p.tolist(), out.tolist(), corner_normals(np, v, f).tolist()


def corner_normals(np, verts, faces):
    """A normal per triangle corner: the area weighted normals round the corner's vertex leaning
    less than CREASE_DEG from this triangle's. (M, 3, 3)."""
    p = verts[faces]
    fn = np.cross(p[:, 1] - p[:, 0], p[:, 2] - p[:, 0])
    unit = fn / np.maximum(np.linalg.norm(fn, axis=1, keepdims=True), 1e-20)
    lim = math.cos(math.radians(CREASE_DEG))
    m = len(faces)
    cface = np.repeat(np.arange(m), 3)
    cvert = faces.reshape(-1)
    order = np.argsort(cvert, kind='stable')
    cv, cf = cvert[order], cface[order]
    starts = np.searchsorted(cv, np.arange(len(verts)))
    deg = np.searchsorted(cv, np.arange(len(verts)), side='right') - starts
    out = np.zeros((m * 3, 3))
    for d in np.unique(deg):
        if d == 0:
            continue
        idx = starts[deg == d][:, None] + np.arange(d)[None, :]
        fr = cf[idx]
        agree = np.einsum('vad,vbd->vab', unit[fr], unit[fr]) >= lim
        out[order[idx].reshape(-1)] = np.einsum('vab,vbd->vad', agree.astype(np.float64), fn[fr]).reshape(-1, 3)
    out /= np.maximum(np.linalg.norm(out, axis=1, keepdims=True), 1e-20)
    return out.reshape(m, 3, 3)


# --- writing ----------------------------------------------------------------------------------------

def glb_bytes(parts, palette):
    """A GLB: a node per part, a primitive per palette colour it uses (positions and normals)."""
    blob = bytearray()
    views, accessors, meshes, nodes = [], [], [], []
    for name, (tris, labels, normals) in parts.items():
        prims = []
        for colour in sorted(set(labels)):
            sel = [i for i, lab in enumerate(labels) if lab == colour]
            attrs = {}
            for key, data in (('POSITION', [p for i in sel for p in tris[i]]),
                              ('NORMAL', [n for i in sel for n in normals[i]])):
                views.append({'buffer': 0, 'byteOffset': len(blob), 'byteLength': len(data) * 12})
                blob += struct.pack(f'<{len(data) * 3}f', *[x for p in data for x in p])
                acc = {'bufferView': len(views) - 1, 'componentType': 5126, 'count': len(data), 'type': 'VEC3'}
                if key == 'POSITION':
                    acc['min'] = [min(p[k] for p in data) for k in range(3)]
                    acc['max'] = [max(p[k] for p in data) for k in range(3)]
                accessors.append(acc)
                attrs[key] = len(accessors) - 1
            prims.append({'attributes': attrs, 'material': colour, 'mode': 4})
        meshes.append({'name': name, 'primitives': prims})
        nodes.append({'name': name, 'mesh': len(meshes) - 1})
    doc = {'asset': {'version': '2.0', 'generator': 'tools/xr/make_gun.py'}, 'scene': 0,
           'scenes': [{'nodes': list(range(len(nodes)))}], 'nodes': nodes, 'meshes': meshes,
           'materials': [{'pbrMetallicRoughness': {'baseColorFactor': [c[0], c[1], c[2], 1.0]}} for c in palette],
           'accessors': accessors, 'bufferViews': views, 'buffers': [{'byteLength': len(blob)}]}
    text = json.dumps(doc, separators=(',', ':')).encode()
    text += b' ' * (-len(text) % 4)
    blob += b'\0' * (-len(blob) % 4)
    return (struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(text) + 8 + len(blob)) +
            struct.pack('<II', len(text), 0x4E4F534A) + text + struct.pack('<II', len(blob), 0x004E4942) + bytes(blob))


def read(path):
    with open(path, 'rb') as f:
        data = f.read()
    tris, image_loader, notes, names = make_model.read_gltf(path, data)
    if not tris:
        raise GunError(f'{path} has no triangles')
    return tris, image_loader, notes, names


def convert(path, prof, out_dir):
    all_tris, image_loader, notes, _ = read(path)
    rows = rotation(prof['forward'], prof['up'])
    tris = pistol_only(all_tris, prof, rows)
    colours = triangle_colours(tris, image_loader, prof.get('node_colours') or {})
    pts, s, off = placed(tris, prof, rows)
    trig = set(split_trigger(tris, pts, prof, off))
    palette, labels = quantise(colours, [area(t) for t in pts], int(prof['colours']))
    normals = own_normals(pts, tris, rows)

    def part(idx):
        return [pts[i] for i in idx], [labels[i] for i in idx], [normals[i] for i in idx]
    body, tr = [i for i in range(len(tris)) if i not in trig], sorted(trig)
    parts = {'Body': part(body), 'Trigger': part(tr)}
    budget = int(prof['triangles'])
    if len(tris) > budget:
        parts['Body'] = simplify(parts['Body'][0], parts['Body'][1], budget - len(tr))
    bp = [p for t in parts['Trigger'][0] for p in t]
    if prof.get('trigger_pivot'):
        pivot = [prof['trigger_pivot'][k] + off[k] for k in range(3)]
    else:
        pivot = [(min(p[0] for p in bp) + max(p[0] for p in bp)) / 2, max(p[1] for p in bp), min(p[2] for p in bp)]
    piv = ':'.join(f'{c:.6f}' for c in pivot)
    rng = ':'.join(f'{c:g}' for c in TRIGGER_RANGE)
    comp = f'trigger=Trigger,input={TRIGGER_INPUT},motion=rotate,axis={TRIGGER_AXIS},pivot={piv},range={rng}'
    os.makedirs(out_dir, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        glb = os.path.join(tmp, 'gun.glb')
        with open(glb, 'wb') as f:
            f.write(glb_bytes(parts, palette))
        for name, mirror in (('gun_right.model', False), ('gun_left.model', True)):
            make_model.main(['--in', glb, '--out', os.path.join(out_dir, name), '--component', comp] +
                            (['--mirror'] if mirror else []))
    for n in sorted(notes):
        print(f'make_gun: note: {n}', file=sys.stderr)
    print(f'make_gun: {len(all_tris)} triangles in the file, {len(tris)} of the pistol, out {len(parts["Body"][0])} '
          f'body + {len(tr)} trigger; scale {s:.6g}, {len(palette)} colours, trigger pivot {piv}')


def inspect(path, prof):
    """What the profile needs: each top node's triangles and bounds in the turned model."""
    tris, _, _, names = read(path)
    rows = rotation(prof['forward'], prof['up'])
    by = {}
    for t in tris:
        key = t.names[-1:]
        by.setdefault(key, []).append([tuple(dot(r, c[0]) for r in rows) for c in t.corners])
    for key, ts in sorted(by.items()):
        pts = [p for t in ts for p in t]
        lo = [min(p[k] for p in pts) for k in range(3)]
        hi = [max(p[k] for p in pts) for k in range(3)]
        print(f'{" / ".join(key) or "(no name)"}: {len(ts)} triangles, x {lo[0]:.4g}..{hi[0]:.4g} '
              f'y {lo[1]:.4g}..{hi[1]:.4g} z {lo[2]:.4g}..{hi[2]:.4g}')
    print(f'{len(tris)} triangles; names: {", ".join(names)}')


def main(argv):
    ap = argparse.ArgumentParser(description='Converts the Gun model into gun_right.model and gun_left.model. '
                                             'See the top of this file.')
    ap.add_argument('--in', dest='input', required=True, metavar='MODEL', help='.glb or .gltf')
    ap.add_argument('--profile', metavar='JSON', help='replaces PROFILE\'s keys')
    ap.add_argument('--inspect', action='store_true', help='print the nodes and their bounds, write nothing')
    ap.add_argument('out_dir', nargs='?', help='where gun_right.model and gun_left.model go')
    args = ap.parse_args(argv)
    prof = dict(PROFILE)
    try:
        if args.profile:
            # another model: nothing of the G17's carries over but the defaults
            prof = dict(NEUTRAL)
            with open(args.profile) as f:
                prof.update(json.load(f))
        if args.inspect:
            inspect(args.input, prof)
        elif args.out_dir:
            convert(args.input, prof, args.out_dir)
        else:
            ap.error('give the folder to write to (or --inspect)')
    except (GunError, make_model.ModelError, OSError, ValueError) as e:
        sys.exit(f'make_gun: {e}')


if __name__ == '__main__':
    main(sys.argv[1:])
