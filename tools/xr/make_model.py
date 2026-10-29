#!/usr/bin/env python3
"""Converts a glTF, GLB or OBJ model into the client's controller model format (MXFM).

The client can draw a model at each hand's grip. This is the offline half of "custom models": it
reads your model, applies its node transforms, and writes the same textured `.model` file
(MXFM, version 2) that tools/frame-models writes for the Steam Frame's own controllers, which the
client reads the same way. Plain Python 3, nothing to install.

  python3 tools/xr/make_model.py --in gun.glb --out custom_right.model
  python3 tools/xr/make_model.py --in gun.glb --out custom_left.model --mirror

usage: make_model.py --in model.glb|.gltf|.obj --out custom.model [--scale S] [--mirror]
                     [--tex texture.png] [--component NAME=NODE[,key=value ...]]

The model is placed as it is authored, so author it in OpenXR's grip space, in metres: the origin
is where the palm closes round the handle, +x to the right, +y up, -z forward along a straightened
index finger (glTF's own axes). --scale S multiplies every position (0.01 for a model in
centimetres). --mirror turns a right hand model into the left hand's (x negated, winding flipped,
tangent frames and component motion mirrored with it).

What is converted: positions, normals (flat ones are made for a model without any), texture
coordinates, triangles (OBJ polygons are fanned from their first corner, glTF triangle strips and
fans are unrolled), and, for glTF, every node transform of the default scene. Vertices are welded
by position, normal and texture coordinate. The textured loader takes one image pair per hand, so
the whole model gets one base colour image:
  - --tex texture.png (8 or 16 bit, not interlaced), used as it is, for any model with texture
    coordinates;
  - else the glTF's own base colour texture (PNG) when it has exactly one;
  - else a palette image, one texel per glTF material (its baseColorFactor), with each triangle's
    coordinates on its material's texel, so a model of several flat colours converts as it is
    (a model with no material at all is white).
The normal map is flat and the roughness fixed: glTF normal, roughness, emissive and vertex colour
data is not used, and the client lights the model itself.
Refused with a message: skins, morph targets, sparse accessors, Draco/meshopt compression, points
and lines, KHR_texture_transform on a used texture, more than one distinct texture or a mix of
textures and flat colours (merge them into one atlas and pass it with --tex), JPEG textures.

--component NAME=NODE moves a node of the model (a glTF node or mesh name with everything below it;
an OBJ o or g name) out of the body into its own component NAME, so the client can move it. Repeat
for more (up to 31). Without more it does not move. What moves it:
  input=PATH   the control, one of /input/trigger /input/grip /input/squeeze /input/a/click
               /input/b/click /input/x/click /input/y/click /input/menu/click /input/view/click
               /input/system/click /input/bumper/click /input/thumbstick /input/dpad_up/click
               (the client reads them from its own OpenXR actions)
  motion=rotate|translate   default rotate when input= is given
  axis=x|y|z|-x|-y|-z|X:Y:Z   default x (a rotation about x swings a trigger's foot back)
  pivot=X:Y:Z  the rotation's centre, metres in the final model (before --mirror); default the top
               centre of the node's bounds, where a trigger hinges
  range=A:B    degrees (rotate) or metres (translate) at input 0 and at input 1; default 0:-25
               for rotate, 0:-0.008 for translate
e.g. --component trigger=Trigger,input=/input/trigger,pivot=0:0.01:-0.02,range=0:-30

Output (little endian, 4 byte fields; rooms/room_gpu.c reads it, mlxr_model_file_parse and
openTextured; tools/frame-models/frame-controller-models.py documents it in full): header 32 bytes,
component records of 208 bytes (body first, then --component in order), vertices of 12 floats
(position 3, normal 3, tangent 4, uv 2 with v from the image's top row down), uint32 indices, then
the colour image and the surface image, RGBA8, texWidth x texHeight each.
"""
import argparse
import base64
import importlib.util
import json
import math
import os
import re
import struct
import sys
import urllib.parse
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
FRAME_CONVERTER = os.path.join(HERE, '..', 'frame-models', 'frame-controller-models.py')

# Where the client stops (rooms/room_gpu.c mlxr_model_file_parse, MLXR_MODEL_COMPONENTS_MAX)
MAX_VERTICES = (1 << 22) - 1
MAX_INDICES = 1 << 24
MAX_COMPONENTS = 32
MAX_TEXTURE = 8192

# The inputs rooms/room_gpu.c motionInput() maps to a control
INPUT_PATHS = ('/input/trigger', '/input/trigger/value', '/input/grip', '/input/squeeze',
               '/input/squeeze/value', '/input/thumbstick', '/input/a/click', '/input/b/click',
               '/input/x/click', '/input/y/click', '/input/menu/click', '/input/view/click',
               '/input/system/click', '/input/bumper/click', '/input/dpad_up/click')

SUPPORTED_REQUIRED = ('KHR_mesh_quantization', 'KHR_texture_transform')  # and KHR_materials_*


class ModelError(Exception):
    pass


def load_frame_converter():
    """tools/frame-models/frame-controller-models.py as a module: its vertex builder (welding and
    tangent frames) and its file writer, so a custom model is written exactly as the Steam Frame's
    are. Nothing runs on import (its GdkPixbuf use is lazy)."""
    if not os.path.isfile(FRAME_CONVERTER):
        raise ModelError(f'{FRAME_CONVERTER} is missing: make_model.py reuses its MXFM writer')
    spec = importlib.util.spec_from_file_location('frame_controller_models', FRAME_CONVERTER)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


# --- small vector and matrix maths ----------------------------------------------------------------

def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def normalize(v, fallback=(0.0, 1.0, 0.0)):
    n = math.sqrt(dot(v, v))
    return (v[0] / n, v[1] / n, v[2] / n) if n > 1e-12 else fallback


def mat_identity():
    return [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0]


def mat_mul(a, b):
    """a * b for column major 4x4s."""
    return [sum(a[k * 4 + r] * b[c * 4 + k] for k in range(4)) for c in range(4) for r in range(4)]


def node_matrix(node):
    if 'matrix' in node:
        m = [float(x) for x in node['matrix']]
        if len(m) != 16:
            raise ModelError('a node matrix does not have 16 numbers')
        return m
    t = node.get('translation', [0, 0, 0])
    q = node.get('rotation', [0, 0, 0, 1])
    s = node.get('scale', [1, 1, 1])
    qn = math.sqrt(sum(c * c for c in q)) or 1.0
    x, y, z, w = (c / qn for c in q)
    r = [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
         [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
         [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]
    m = mat_identity()
    for c in range(3):
        for row in range(3):
            m[c * 4 + row] = r[row][c] * s[c]
    m[12], m[13], m[14] = t
    return m


def transform_point(m, p):
    return tuple(m[0 + r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r] for r in range(3))


def normal_matrix(m):
    """The 3x3 that carries normals (the cofactor of the upper 3x3, its sign kept), as three
    columns, and whether the transform mirrors (a negative determinant)."""
    c0, c1, c2 = tuple(m[0:3]), tuple(m[4:7]), tuple(m[8:11])
    det = dot(c0, cross(c1, c2))
    sign = -1.0 if det < 0 else 1.0
    return (cross(c1, c2), cross(c2, c0), cross(c0, c1)), sign, det < 0


def transform_normal(nm, n):
    cols, sign, _ = nm
    return normalize(tuple(sign * (cols[0][r] * n[0] + cols[1][r] * n[1] + cols[2][r] * n[2])
                           for r in range(3)))


def srgb_byte(linear):
    c = min(max(linear, 0.0), 1.0)
    c = c * 12.92 if c <= 0.0031308 else 1.055 * c ** (1 / 2.4) - 0.055
    return int(round(min(max(c, 0.0), 1.0) * 255))


# --- a triangle of the model ------------------------------------------------------------------------
#
# Tri: corners [(position, uv or None, normal or None) x3], uv with v running down the image (glTF's
# own convention; OBJ's is turned on reading), names: the node/mesh/object names it sits under,
# material: ('color', (r, g, b)) linear, or ('image', key), or None.

class Tri:
    __slots__ = ('corners', 'names', 'material')

    def __init__(self, corners, names, material):
        self.corners = corners
        self.names = names
        self.material = material


# --- glTF -------------------------------------------------------------------------------------------

COMPONENT_FORMAT = {5120: 'b', 5121: 'B', 5122: 'h', 5123: 'H', 5125: 'I', 5126: 'f'}
TYPE_COUNT = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}
NORMALIZE_DIVISOR = {5120: 127.0, 5121: 255.0, 5122: 32767.0, 5123: 65535.0}


class Gltf:
    def __init__(self, doc, base_dir, glb_bin):
        self.doc = doc
        self.base_dir = base_dir
        self.buffers = {}
        self.glb_bin = glb_bin

    def uri_bytes(self, uri, what):
        if uri.startswith('data:'):
            head, _, payload = uri.partition(',')
            if ';base64' not in head:
                raise ModelError(f'{what}: only base64 data URIs are read')
            try:
                return base64.b64decode(payload)
            except ValueError as e:
                raise ModelError(f'{what}: bad base64 data ({e})')
        if re.match(r'^[a-zA-Z][a-zA-Z0-9+.-]*:', uri) or uri.startswith(('/', '\\')):
            raise ModelError(f'{what}: {uri!r} is not a file next to the model')
        path = os.path.normpath(os.path.join(self.base_dir, urllib.parse.unquote(uri)))
        root = os.path.normpath(self.base_dir)
        if os.path.commonpath([root, path]) != root:
            raise ModelError(f'{what}: {uri!r} points outside the model\'s folder')
        try:
            with open(path, 'rb') as f:
                return f.read()
        except OSError as e:
            raise ModelError(f'{what}: cannot read {uri!r} ({e.strerror})')

    def buffer(self, index):
        if index not in self.buffers:
            buffers = self.doc.get('buffers', [])
            if not 0 <= index < len(buffers):
                raise ModelError(f'buffer {index} does not exist')
            b = buffers[index]
            if 'uri' in b:
                data = self.uri_bytes(b['uri'], f'buffer {index}')
            elif index == 0 and self.glb_bin is not None:
                data = self.glb_bin
            else:
                raise ModelError(f'buffer {index} has no data')
            if len(data) < b.get('byteLength', 0):
                raise ModelError(f'buffer {index} is shorter than its byteLength')
            self.buffers[index] = data
        return self.buffers[index]

    def view_bytes(self, index):
        views = self.doc.get('bufferViews', [])
        if not 0 <= index < len(views):
            raise ModelError(f'bufferView {index} does not exist')
        v = views[index]
        data = self.buffer(v['buffer'])
        start, length = v.get('byteOffset', 0), v['byteLength']
        if start + length > len(data):
            raise ModelError(f'bufferView {index} runs past its buffer')
        return data, start, length, v.get('byteStride')

    def accessor(self, index):
        """A list of tuples (floats, or ints for indices), normalised ints turned to 0..1 / -1..1."""
        accessors = self.doc.get('accessors', [])
        if not 0 <= index < len(accessors):
            raise ModelError(f'accessor {index} does not exist')
        a = accessors[index]
        if 'sparse' in a:
            raise ModelError(f'accessor {index} is sparse, which is not supported')
        ctype, kind, count = a['componentType'], a['type'], a['count']
        if ctype not in COMPONENT_FORMAT or kind not in TYPE_COUNT:
            raise ModelError(f'accessor {index}: {kind} of component type {ctype} is not supported')
        n = TYPE_COUNT[kind]
        fmt = '<' + COMPONENT_FORMAT[ctype] * n
        size = struct.calcsize(fmt)
        if 'bufferView' not in a:
            return [tuple([0] * n) for _ in range(count)]
        data, start, length, stride = self.view_bytes(a['bufferView'])
        stride = stride or size
        first = start + a.get('byteOffset', 0)
        if count and first + stride * (count - 1) + size > start + length:
            raise ModelError(f'accessor {index} runs past its bufferView')
        out = [struct.unpack_from(fmt, data, first + stride * i) for i in range(count)]
        if a.get('normalized') and ctype in NORMALIZE_DIVISOR:
            d = NORMALIZE_DIVISOR[ctype]
            out = [tuple(max(c / d, -1.0) for c in e) for e in out]
        return out


def read_glb(data):
    if len(data) < 20 or struct.unpack_from('<I', data, 0)[0] != 0x46546C67:
        raise ModelError('not a GLB file (bad magic)')
    version, total = struct.unpack_from('<II', data, 4)
    if version != 2:
        raise ModelError(f'GLB version {version}: only version 2 is read')
    total = min(total, len(data))
    doc, binary, pos = None, None, 12
    while pos + 8 <= total:
        length, kind = struct.unpack_from('<II', data, pos)
        chunk = data[pos + 8:pos + 8 + length]
        if len(chunk) != length:
            raise ModelError('GLB chunk runs past the end of the file')
        if kind == 0x4E4F534A and doc is None:
            doc = json.loads(chunk.decode('utf-8'))
        elif kind == 0x004E4942 and binary is None:
            binary = chunk
        pos += 8 + length + (-length % 4)
    if doc is None:
        raise ModelError('GLB has no JSON chunk')
    return doc, binary


def check_gltf_support(doc):
    for ext in doc.get('extensionsRequired', []):
        if ext not in SUPPORTED_REQUIRED and not ext.startswith('KHR_materials_'):
            raise ModelError(f'the model requires the glTF extension {ext}, which is not supported '
                             '(Draco and meshopt compression need to be undone first)')


def gltf_material(g, index, notes):
    """The material key of a primitive: ('color', rgb) or ('image', n)."""
    if index is None:
        return ('color', (1.0, 1.0, 1.0))
    materials = g.doc.get('materials', [])
    if not 0 <= index < len(materials):
        raise ModelError(f'material {index} does not exist')
    pbr = materials[index].get('pbrMetallicRoughness', {})
    factor = pbr.get('baseColorFactor', [1.0, 1.0, 1.0, 1.0])
    tex = pbr.get('baseColorTexture')
    if tex is None:
        return ('color', (float(factor[0]), float(factor[1]), float(factor[2])))
    if tex.get('texCoord', 0) != 0:
        raise ModelError(f'material {index}: its base colour texture uses TEXCOORD_{tex["texCoord"]}, '
                         'only TEXCOORD_0 is read')
    if 'KHR_texture_transform' in tex.get('extensions', {}):
        raise ModelError(f'material {index}: KHR_texture_transform on the base colour texture is not '
                         'supported (bake the transform into the UVs)')
    textures = g.doc.get('textures', [])
    if not 0 <= tex['index'] < len(textures) or 'source' not in textures[tex['index']]:
        raise ModelError(f'material {index}: its texture has no PNG source (only plain image sources are read)')
    if any(abs(f - 1.0) > 1e-6 for f in factor[:3]):
        notes.add(f'material {index}: baseColorFactor is ignored on a textured material')
    return ('image', textures[tex['index']]['source'])


def read_gltf(path, data):
    base_dir = os.path.dirname(os.path.abspath(path))
    if data[:4] == b'glTF':
        doc, glb_bin = read_glb(data)
    else:
        try:
            doc, glb_bin = json.loads(data.decode('utf-8-sig')), None
        except (ValueError, UnicodeDecodeError) as e:
            raise ModelError(f'{path} is not valid glTF JSON ({e})')
    if doc.get('asset', {}).get('version', '2.0')[:1] != '2':
        raise ModelError('only glTF 2.0 is read')
    check_gltf_support(doc)
    g = Gltf(doc, base_dir, glb_bin)
    notes = set()
    if doc.get('animations'):
        notes.add('glTF animations are ignored')

    nodes = doc.get('nodes', [])
    if 'scenes' in doc and doc['scenes']:
        scene = doc['scenes'][doc.get('scene', 0)]
        roots = scene.get('nodes', [])
    else:
        children = {c for n in nodes for c in n.get('children', [])}
        roots = [i for i in range(len(nodes)) if i not in children]

    tris = []
    meshes = doc.get('meshes', [])
    cache = {}

    def mesh_primitives(mi):
        # (positions, normals or None, uvs or None, indices, material key) per primitive, once
        if mi in cache:
            return cache[mi]
        if not 0 <= mi < len(meshes):
            raise ModelError(f'mesh {mi} does not exist')
        mesh = meshes[mi]
        if any(w for w in mesh.get('weights', [])):
            raise ModelError(f'mesh {mesh.get("name", mi)!r} has morph target weights: morph targets '
                             'are not supported')
        prims = []
        for pi, prim in enumerate(mesh['primitives']):
            where = f'mesh {mesh.get("name", mi)!r} primitive {pi}'
            if prim.get('targets'):
                raise ModelError(f'{where} has morph targets, which are not supported')
            if 'KHR_draco_mesh_compression' in prim.get('extensions', {}):
                raise ModelError(f'{where} is Draco compressed, which is not supported')
            attrs = prim['attributes']
            if 'JOINTS_0' in attrs or 'WEIGHTS_0' in attrs:
                raise ModelError(f'{where} is skinned, which is not supported')
            mode = prim.get('mode', 4)
            if mode not in (4, 5, 6):
                raise ModelError(f'{where} is drawn as points or lines (mode {mode}), only triangles are read')
            if 'POSITION' not in attrs:
                raise ModelError(f'{where} has no POSITION')
            pos = g.accessor(attrs['POSITION'])
            nrm = g.accessor(attrs['NORMAL']) if 'NORMAL' in attrs else None
            uv = g.accessor(attrs['TEXCOORD_0']) if 'TEXCOORD_0' in attrs else None
            if (nrm is not None and len(nrm) != len(pos)) or (uv is not None and len(uv) != len(pos)):
                raise ModelError(f'{where}: attributes of different lengths')
            if 'COLOR_0' in attrs:
                notes.add('vertex colours (COLOR_0) are ignored: the textured model has none')
            if 'indices' in prim:
                idx = [i[0] for i in g.accessor(prim['indices'])]
            else:
                idx = list(range(len(pos)))
            if idx and max(idx) >= len(pos):
                raise ModelError(f'{where}: an index is past the last vertex')
            if mode == 5:
                idx = [t for k in range(len(idx) - 2)
                       for t in ((idx[k], idx[k + 1], idx[k + 2]) if k % 2 == 0
                                 else (idx[k + 1], idx[k], idx[k + 2]))]
            elif mode == 6:
                idx = [t for k in range(1, len(idx) - 1) for t in (idx[0], idx[k], idx[k + 1])]
            if len(idx) % 3:
                raise ModelError(f'{where}: the index count is not a multiple of 3')
            mat = gltf_material(g, prim.get('material'), notes)
            prims.append((pos, nrm, uv, idx, mat))
        cache[mi] = prims
        return prims

    def walk(ni, parent, names, depth):
        if depth > 64 or not 0 <= ni < len(nodes):
            raise ModelError('the node tree is too deep or refers to a node that does not exist')
        node = nodes[ni]
        if 'skin' in node:
            raise ModelError(f'node {node.get("name", ni)!r} has a skin: skinned meshes are not supported')
        if 'EXT_mesh_gpu_instancing' in node.get('extensions', {}):
            raise ModelError('EXT_mesh_gpu_instancing is not supported')
        world = mat_mul(parent, node_matrix(node))
        here = names + ((node['name'],) if 'name' in node else ())
        if 'mesh' in node:
            mesh = meshes[node['mesh']] if 0 <= node['mesh'] < len(meshes) else {}
            mnames = here + ((mesh['name'],) if 'name' in mesh else ())
            nm = normal_matrix(world)
            flip = nm[2]
            for pos, nrm, uv, idx, mat in mesh_primitives(node['mesh']):
                wpos = [transform_point(world, p) for p in pos]
                wnrm = [transform_normal(nm, n) for n in nrm] if nrm is not None else None
                for t in range(0, len(idx), 3):
                    order = (0, 2, 1) if flip else (0, 1, 2)
                    corners = []
                    for k in order:
                        i = idx[t + k]
                        corners.append((wpos[i], tuple(uv[i][:2]) if uv is not None else None,
                                        wnrm[i] if wnrm is not None else None))
                    tris.append(Tri(corners, mnames, mat))
        for child in node.get('children', []):
            walk(child, world, here, depth + 1)

    for r in roots:
        walk(r, mat_identity(), (), 0)

    def image_loader(key):
        images = doc.get('images', [])
        if not 0 <= key < len(images):
            raise ModelError(f'image {key} does not exist')
        im = images[key]
        if 'uri' in im:
            return g.uri_bytes(im['uri'], f'image {key}')
        if 'bufferView' in im:
            data, start, length, _ = g.view_bytes(im['bufferView'])
            return data[start:start + length]
        raise ModelError(f'image {key} has no data')

    names = sorted({n for t in tris for n in t.names})
    return tris, image_loader, notes, names


# --- OBJ --------------------------------------------------------------------------------------------

def read_obj(path, data):
    try:
        text = data.decode('utf-8-sig')
    except UnicodeDecodeError:
        text = data.decode('latin-1')
    text = text.replace('\\\r\n', ' ').replace('\\\n', ' ')
    vs, vts, vns, tris = [], [], [], []
    notes = set()
    obj_name, groups, materials = '', (), set()
    used_names = set()
    for number, line in enumerate(text.splitlines(), 1):
        parts = line.split('#', 1)[0].split()
        if not parts:
            continue
        key = parts[0]
        try:
            if key == 'v':
                vs.append(tuple(float(x) for x in parts[1:4]))
                if len(vs[-1]) != 3:
                    raise ValueError('needs x y z')
            elif key == 'vt':
                vts.append((float(parts[1]), float(parts[2]) if len(parts) > 2 else 0.0))
            elif key == 'vn':
                vns.append(tuple(float(x) for x in parts[1:4]))
                if len(vns[-1]) != 3:
                    raise ValueError('needs x y z')
            elif key == 'o':
                obj_name, groups = ' '.join(parts[1:]), ()
            elif key == 'g':
                groups = tuple(parts[1:])
            elif key == 'usemtl':
                materials.add(' '.join(parts[1:]))
            elif key == 'f':
                corners = []
                for ref in parts[1:]:
                    f = (ref.split('/') + ['', ''])[:3]

                    def pick(s, table, what):
                        if not s:
                            return None
                        i = int(s)
                        i = i - 1 if i > 0 else len(table) + i
                        if not 0 <= i < len(table):
                            raise ValueError(f'{what} index {s} does not exist')
                        return table[i]
                    p = pick(f[0], vs, 'vertex')
                    if p is None:
                        raise ValueError('a face corner without a vertex')
                    t = pick(f[1], vts, 'texture coordinate')
                    corners.append((p, (t[0], 1.0 - t[1]) if t else None, pick(f[2], vns, 'normal')))
                if len(corners) < 3:
                    raise ValueError('a face needs at least 3 corners')
                names = tuple(n for n in ((obj_name,) + groups) if n)
                used_names.update(names)
                for k in range(1, len(corners) - 1):
                    tris.append(Tri([corners[0], corners[k], corners[k + 1]], names, None))
            elif key in ('l', 'p'):
                raise ValueError('lines and points are not supported')
        except (ValueError, IndexError) as e:
            raise ModelError(f'{path}, line {number}: {e}')
    if len(materials) > 1:
        notes.add('OBJ materials are ignored (pass --tex for a texture)')
    elif materials:
        notes.add('the OBJ material is ignored (pass --tex for a texture)')
    return tris, None, notes, sorted(used_names)


# --- PNG --------------------------------------------------------------------------------------------

PNG_SIGNATURE = b'\x89PNG\r\n\x1a\n'


def read_png(data, what):
    """(rgb bytes, width, height) of a non-interlaced PNG; alpha is dropped (the model's alpha is
    its ambient occlusion). Needs only zlib."""
    if data[:8] != PNG_SIGNATURE:
        raise ModelError(f'{what} is not a PNG (convert JPEG and other images to PNG first)')
    pos, header, palette, idat = 8, None, None, []
    while pos + 8 <= len(data):
        length, kind = struct.unpack_from('>I4s', data, pos)
        body = data[pos + 8:pos + 8 + length]
        if len(body) != length:
            raise ModelError(f'{what}: PNG chunk runs past the end of the file')
        if kind == b'IHDR':
            header = struct.unpack('>IIBBBBB', body)
        elif kind == b'PLTE':
            palette = body
        elif kind == b'IDAT':
            idat.append(body)
        elif kind == b'IEND':
            break
        pos += 12 + length
    if header is None or not idat:
        raise ModelError(f'{what}: PNG has no header or no image data')
    w, h, depth, ctype, _, _, interlace = header
    if interlace:
        raise ModelError(f'{what}: interlaced PNGs are not supported (save it without interlacing)')
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(ctype)
    if channels is None or depth not in (1, 2, 4, 8, 16) or (ctype != 0 and depth < 8 and ctype != 3) \
            or (ctype in (2, 4, 6) and depth < 8):
        raise ModelError(f'{what}: PNG colour type {ctype} at {depth} bits is not supported')
    if not 0 < w <= MAX_TEXTURE or not 0 < h <= MAX_TEXTURE:
        raise ModelError(f'{what}: the texture is {w}x{h}, the client reads up to {MAX_TEXTURE}')
    bits = channels * depth
    stride = (w * bits + 7) // 8
    bpp = max(1, bits // 8)
    try:
        raw = zlib.decompress(b''.join(idat))
    except zlib.error as e:
        raise ModelError(f'{what}: bad PNG image data ({e})')
    if len(raw) < (stride + 1) * h:
        raise ModelError(f'{what}: PNG image data is short')

    rows = []
    prev = bytearray(stride)
    for y in range(h):
        ftype = raw[y * (stride + 1)]
        cur = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        if ftype == 1:
            for i in range(bpp, stride):
                cur[i] = (cur[i] + cur[i - bpp]) & 255
        elif ftype == 2:
            for i in range(stride):
                cur[i] = (cur[i] + prev[i]) & 255
        elif ftype == 3:
            for i in range(stride):
                left = cur[i - bpp] if i >= bpp else 0
                cur[i] = (cur[i] + ((left + prev[i]) >> 1)) & 255
        elif ftype == 4:
            for i in range(stride):
                a = cur[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                cur[i] = (cur[i] + pred) & 255
        elif ftype != 0:
            raise ModelError(f'{what}: PNG row filter {ftype} does not exist')
        rows.append(cur)
        prev = cur

    out = bytearray(w * h * 3)
    for y, row in enumerate(rows):
        o = y * w * 3
        if depth == 8 and ctype == 2:
            out[o:o + w * 3] = row[:w * 3]
        elif depth == 8 and ctype == 6:
            out[o:o + w * 3:3] = row[0:w * 4:4]
            out[o + 1:o + w * 3:3] = row[1:w * 4:4]
            out[o + 2:o + w * 3:3] = row[2:w * 4:4]
        elif depth == 8 and ctype == 0:
            for k in range(3):
                out[o + k:o + w * 3:3] = row[:w]
        elif depth == 8 and ctype == 4:
            for k in range(3):
                out[o + k:o + w * 3:3] = row[0:w * 2:2]
        elif depth == 16:
            step = channels * 2
            if ctype in (2, 6):
                for k in range(3):
                    out[o + k:o + w * 3:3] = row[k * 2:w * step:step]
            else:
                for k in range(3):
                    out[o + k:o + w * 3:3] = row[0:w * step:step]
        else:  # 1, 2 or 4 bit grey or palette
            per = 8 // depth
            mask = (1 << depth) - 1
            for x in range(w):
                v = (row[x // per] >> (8 - depth * (x % per + 1))) & mask
                if ctype == 3:
                    if palette is None or v * 3 + 2 >= len(palette):
                        raise ModelError(f'{what}: PNG palette entry {v} is missing')
                    rgb = palette[v * 3:v * 3 + 3]
                else:
                    rgb = bytes([v * 255 // mask] * 3)
                out[o + x * 3:o + x * 3 + 3] = rgb
    return bytes(out), w, h


# --- assembling the model -------------------------------------------------------------------------------

def parse_vec(text, what, count=3):
    parts = text.split(':')
    if len(parts) != count:
        raise ModelError(f'{what}: expected {count} numbers separated by ":", got {text!r}')
    try:
        return tuple(float(x) for x in parts)
    except ValueError:
        raise ModelError(f'{what}: {text!r} is not numbers separated by ":"')


AXES = {'x': (1.0, 0.0, 0.0), 'y': (0.0, 1.0, 0.0), 'z': (0.0, 0.0, 1.0),
        '-x': (-1.0, 0.0, 0.0), '-y': (0.0, -1.0, 0.0), '-z': (0.0, 0.0, -1.0)}


def parse_component(spec):
    head, _, rest = spec.partition(',')
    name, eq, node = head.partition('=')
    if not eq or not name or not node:
        raise ModelError(f'--component {spec!r}: expected NAME=NODE[,key=value ...]')
    if len(name.encode('utf-8')) > 31:
        raise ModelError(f'--component {name!r}: component names are at most 31 bytes')
    if name == 'body':
        raise ModelError('--component: "body" is the rest of the model; pick another name')
    c = {'name': name, 'node': node, 'input': '', 'motion': None, 'axis': None, 'pivot': None, 'range': None}
    for item in filter(None, rest.split(',')):
        key, eq, value = item.partition('=')
        if not eq:
            raise ModelError(f'--component {name}: {item!r} is not key=value')
        if key == 'input':
            if value not in INPUT_PATHS:
                raise ModelError(f'--component {name}: input {value!r} is not one the client reads; '
                                 f'use one of {", ".join(INPUT_PATHS)}')
            c['input'] = value
        elif key == 'motion':
            if value not in ('rotate', 'translate'):
                raise ModelError(f'--component {name}: motion is rotate or translate, not {value!r}')
            c['motion'] = value
        elif key == 'axis':
            c['axis'] = AXES.get(value) or parse_vec(value, f'--component {name} axis')
            if dot(c['axis'], c['axis']) < 1e-12:
                raise ModelError(f'--component {name}: the axis has no length')
        elif key == 'pivot':
            c['pivot'] = parse_vec(value, f'--component {name} pivot')
        elif key == 'range':
            c['range'] = parse_vec(value, f'--component {name} range', 2)
        else:
            raise ModelError(f'--component {name}: unknown key {key!r} '
                             '(input, motion, axis, pivot, range)')
    if c['motion'] and not c['input']:
        raise ModelError(f'--component {name}: motion needs input=')
    if c['input'] and not c['motion']:
        c['motion'] = 'rotate'
    if not c['input'] and (c['axis'] or c['pivot'] or c['range']):
        raise ModelError(f'--component {name}: axis, pivot and range need input=')
    return c


def bounds(points):
    return ([min(p[k] for p in points) for k in range(3)], [max(p[k] for p in points) for k in range(3)])


def motion_floats(conv, comp, points, mirror):
    """(type, motion floats) of a component in the final model (before the mirror turns it)."""
    f = [0.0] * conv.MOTION_FLOATS
    f[conv.MOTION_PRESSED] = float('nan')
    if not comp['input']:
        return 0, f
    lo, hi = bounds(points)
    axis = normalize(comp['axis'] or AXES['x'], AXES['x'])
    if comp['motion'] == 'rotate':
        pivot = comp['pivot'] or ((lo[0] + hi[0]) / 2, hi[1], (lo[2] + hi[2]) / 2)
        rng = comp['range'] or (0.0, -25.0)
        f[conv.MOTION_ORIGIN:conv.MOTION_ORIGIN + 3] = pivot
    else:
        rng = comp['range'] or (0.0, -0.008)
    f[conv.MOTION_AXIS:conv.MOTION_AXIS + 3] = axis
    f[conv.MOTION_RANGE:conv.MOTION_RANGE + 2] = rng
    return conv.MOTION_TYPES[comp['motion']], f


def mirror_motion(conv, kind, f):
    """The motion of the mirrored part: points across x, and a rotation about an axis a turns, as
    seen in the mirror, about -(a mirrored) by the same angle."""
    if kind == conv.MOTION_TYPES['rotate']:
        f[conv.MOTION_ORIGIN] = -f[conv.MOTION_ORIGIN]
        f[conv.MOTION_AXIS + 1] = -f[conv.MOTION_AXIS + 1]
        f[conv.MOTION_AXIS + 2] = -f[conv.MOTION_AXIS + 2]
    elif kind == conv.MOTION_TYPES['translate']:
        f[conv.MOTION_AXIS] = -f[conv.MOTION_AXIS]


def build_texture(tris, tex_png, image_loader):
    """Decides the one base colour image. Returns (rgb bytes, w, h, palette) where palette maps a
    triangle's material to a texel x (or None when the model's own UVs are used)."""
    if tex_png is not None:
        rgb, w, h = read_png(tex_png, '--tex image')
        return rgb, w, h, None
    keys = []
    for t in tris:
        m = t.material or ('color', (1.0, 1.0, 1.0))
        if m not in keys:
            keys.append(m)
    images = [k for k in keys if k[0] == 'image']
    if len(images) == 1 and len(keys) == 1:
        rgb, w, h = read_png(image_loader(images[0][1]), 'the base colour texture')
        return rgb, w, h, None
    if images:
        raise ModelError('the model uses ' + ('several textures' if len(images) > 1 else 'a texture and flat colours')
                         + ': the client draws a model with one image. Merge them into one atlas '
                           'and pass it with --tex (the UVs must already point into it)')
    if len(keys) > MAX_TEXTURE:
        raise ModelError(f'{len(keys)} materials is more than a palette image can hold')
    rgb = bytearray()
    for k in keys:
        rgb += bytes(srgb_byte(c) for c in k[1])
    return bytes(rgb), len(keys), 1, {k: i for i, k in enumerate(keys)}


def convert(args, conv):
    path = args.input
    try:
        with open(path, 'rb') as f:
            data = f.read()
    except OSError as e:
        raise ModelError(f'cannot read {path}: {e.strerror}')
    ext = os.path.splitext(path)[1].lower()
    if ext in ('.gltf', '.glb'):
        tris, image_loader, notes, node_names = read_gltf(path, data)
    elif ext == '.obj':
        tris, image_loader, notes, node_names = read_obj(path, data)
    else:
        raise ModelError(f'{path}: expected a .glb, .gltf or .obj file')
    if not tris:
        raise ModelError(f'{path} has no triangles')
    if not args.scale > 0 or not math.isfinite(args.scale):
        raise ModelError('--scale must be a positive number')

    specs = [parse_component(s) for s in args.component]
    if len({s['name'] for s in specs}) != len(specs):
        raise ModelError('--component names must differ')
    if len(specs) + 1 > MAX_COMPONENTS:
        raise ModelError(f'at most {MAX_COMPONENTS - 1} components')
    for s in specs:
        if s['node'] not in node_names:
            shown = ', '.join(repr(n) for n in node_names[:30]) or 'none have names'
            raise ModelError(f'--component {s["name"]}: no node or object named {s["node"]!r} with '
                             f'triangles in the model (names: {shown})')

    # Scale, normals, and the groups: body first, then each component in order
    texture, tw, th, palette = build_texture(tris, args.tex_png, image_loader)
    if palette is None and any(c[1] is None for t in tris for c in t.corners):
        raise ModelError('the model has no texture coordinates, which the image needs: '
                         'use a model with UVs, or leave out --tex')
    groups = {None: []}
    for s in specs:
        groups[s['name']] = []
    for t in tris:
        owner = next((s['name'] for s in specs if s['node'] in t.names), None)
        corners = []
        p3 = [tuple(c * args.scale for c in c3[0]) for c3 in t.corners]
        face = normalize(cross(sub(p3[1], p3[0]), sub(p3[2], p3[0])))
        for (p, uv, n), q in zip(t.corners, p3):
            n = normalize(n, face) if n is not None else face
            if palette is not None:
                uv = ((palette[t.material or ('color', (1.0, 1.0, 1.0))] + 0.5) / tw, 0.5)
            corners.append((q, uv, n))
        groups[owner].append(corners)
    for s in specs:
        if not groups[s['name']]:
            raise ModelError(f'--component {s["name"]}: {s["node"]!r} has no triangles')

    components, vertices, indices = [], [], []
    for name in [None] + [s['name'] for s in specs]:
        gtris = groups[name]
        if not gtris:
            continue
        spec = next((s for s in specs if s['name'] == name), None)
        kind, motion = (0, motion_floats(conv, {'input': ''}, [], False)[1]) if spec is None else \
            motion_floats(conv, spec, [c[0] for g in gtris for c in g], args.mirror)
        if args.mirror:
            gtris = [[(( -p[0], p[1], p[2]), uv, (-n[0], n[1], n[2])) for p, uv, n in (g[0], g[2], g[1])]
                     for g in gtris]
            mirror_motion(conv, kind, motion)
        # build_vertices turns v to run from the image's top row, as OBJ and glTF leave it for
        # the file; this model's v already does, so hand it 1 - v to come out as it went in
        fed = [[(p, (uv[0], 1.0 - uv[1]), n) for p, uv, n in g] for g in gtris]
        v, i = conv.build_vertices(fed, len(vertices))
        components.append({'name': name or 'body', 'path': spec['input'] if spec else '',
                           'first': len(indices), 'count': len(i), 'type': kind, 'motion': motion})
        vertices += v
        indices += i
    if len(vertices) > MAX_VERTICES or len(indices) > MAX_INDICES:
        raise ModelError(f'{len(vertices)} vertices and {len(indices) // 3} triangles are more than the '
                         f'client reads ({MAX_VERTICES} and {MAX_INDICES // 3})')

    texels = tw * th
    colour = bytearray(texels * 4)
    colour[0::4], colour[1::4], colour[2::4] = texture[0::3], texture[1::3], texture[2::3]
    colour[3::4] = b'\xff' * texels  # no ambient occlusion
    surface = bytes((128, 128, 255, 128)) * texels  # a flat normal map, middling roughness
    model = {'components': components, 'vertices': vertices, 'indices': indices, 'colour': bytes(colour),
             'surface': surface, 'width': tw, 'height': th}
    return model, notes


def main(argv=None):
    ap = argparse.ArgumentParser(description='Converts glTF, GLB or OBJ into the client\'s MXFM model '
                                             'format. See the top of this file for the details.')
    ap.add_argument('--in', dest='input', required=True, metavar='MODEL', help='.glb, .gltf or .obj')
    ap.add_argument('--out', required=True, metavar='FILE', help='the .model file to write')
    ap.add_argument('--scale', type=float, default=1.0, help='multiplies every position (default 1)')
    ap.add_argument('--mirror', action='store_true', help='mirror in x (a right hand model for the left)')
    ap.add_argument('--tex', metavar='PNG', help='the base colour image (default: the model\'s own)')
    ap.add_argument('--component', action='append', default=[], metavar='NAME=NODE[,key=value...]',
                    help='move a node into its own, animated component (see the file header)')
    args = ap.parse_args(argv)
    args.tex_png = None
    try:
        if args.tex:
            try:
                with open(args.tex, 'rb') as f:
                    args.tex_png = f.read()
            except OSError as e:
                raise ModelError(f'cannot read {args.tex}: {e.strerror}')
        conv = load_frame_converter()
        model, notes = convert(args, conv)
        out_dir = os.path.dirname(os.path.abspath(args.out))
        os.makedirs(out_dir, exist_ok=True)
        conv.write_model(args.out, model)
    except ModelError as e:
        sys.exit(f'make_model: {e}')
    for n in sorted(notes):
        print(f'make_model: note: {n}', file=sys.stderr)
    lo, hi = bounds([v[0:3] for v in model['vertices']])
    print(f'{args.out}: {len(model["vertices"])} vertices, {len(model["indices"]) // 3} triangles, '
          f'{len(model["components"])} components, texture {model["width"]}x{model["height"]}, '
          f'size {hi[0] - lo[0]:.3f} x {hi[1] - lo[1]:.3f} x {hi[2] - lo[2]:.3f} m')


if __name__ == '__main__':
    main()
