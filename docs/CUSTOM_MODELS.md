# Custom controller models

`tools/xr/make_model.py` turns your own 3D model (glTF, GLB or OBJ) into the `.model` file the VR client draws at a hand's grip. It is plain Python 3 with nothing to install, and runs on any computer.

The client reads these files from `~/.config/moonlight-xr/models/` (the Custom controller model kind; until a build with that kind is installed the files are written but not drawn). Your model is yours: nothing here is uploaded or shared, and its licence is your business.

## Convert

```
python3 tools/xr/make_model.py --in pistol.glb --out custom_right.model
python3 tools/xr/make_model.py --in pistol.glb --out custom_left.model --mirror
```

Put the pair in `~/.config/moonlight-xr/models/`. A single `custom.model` (no `_left`/`_right`) is the right hand's model and the client mirrors it for the left. `--mirror` writes the left hand's file for you if you want it as a real pair.

| Option | What it does |
| --- | --- |
| `--in FILE` | `.glb`, `.gltf` (embedded or external `.bin`) or `.obj`. |
| `--out FILE` | The `.model` to write. |
| `--scale S` | Multiplies every position. Use `0.01` for a model in centimetres. |
| `--mirror` | x negated, triangle winding flipped, motion mirrored: a right-hand model becomes the left. |
| `--tex FILE.png` | The base colour image (8 or 16 bit PNG, not interlaced). Needs a model with texture coordinates. |
| `--component NAME=NODE,...` | Moves a node into its own component that the client can animate (below). |

## Author it for the grip

Units are metres. The origin is the middle of the handle where the palm closes round it, +x points to the right, +y up, and -z forward along a straightened index finger (glTF's own axes). Aim the barrel or pointing end along -z. Keep it small: a controller is about 15 cm long.

## What is read

Positions, normals (flat ones are made if the model has none), texture coordinates and triangles. glTF node transforms are applied. OBJ polygons are fanned from their first corner, so keep them convex.

The client draws a model with one colour image, so:

1. `--tex` is used if given.
2. Otherwise the glTF's own base colour texture, if it has exactly one (PNG only).
3. Otherwise a small palette is made from the materials' base colours, so a model of several flat colours works as it is. A model with no material is white.

Normal, roughness, emissive and vertex colour data is not used: the client lights the model itself. Skins, morph targets, sparse accessors, Draco or meshopt compression, lines and points, JPEG textures, and several different textures are refused with a message that says which. For several textures, merge them into one atlas in your 3D tool and pass it with `--tex`.

## Moving parts

```
python3 tools/xr/make_model.py --in pistol.glb --out custom_right.model \
    --component trigger=Trigger,input=/input/trigger
```

`NODE` is a glTF node (or mesh) name, or an OBJ `o`/`g` name; everything below it moves. Without more options the part is drawn still. Options, comma separated:

| Key | Meaning |
| --- | --- |
| `input=PATH` | The control that moves it: `/input/trigger`, `/input/grip`, `/input/a/click`, `/input/b/click`, `/input/x/click`, `/input/y/click`, `/input/menu/click`, `/input/view/click`, `/input/bumper/click`, `/input/thumbstick`, and the others listed at the top of `tools/xr/make_model.py`. |
| `motion=rotate\|translate` | Default `rotate`. |
| `axis=x\|y\|z\|-x\|-y\|-z\|X:Y:Z` | Default `x`: a rotation about x swings the foot of a trigger back toward the palm. |
| `pivot=X:Y:Z` | Rotation centre in metres, before `--mirror`. Default: the top centre of the part's bounds, where a trigger hinges. |
| `range=A:B` | Degrees (rotate) or metres (translate) at input 0 and at input 1. Default `0:-25` for rotate, `0:-0.008` for translate. |

Example with everything given: `--component trigger=Trigger,input=/input/trigger,axis=x,pivot=0:0.012:-0.02,range=0:-30`.

Components are written in this order: `body` (everything not named), then your components in the order given, at most 31.

## Look at the result

The converter prints the vertex and triangle count, the number of components and the model's size. The size should read like a hand-held object (centimetres to a few tens of centimetres, as metres). If it is 100 times too big or small, adjust `--scale`.
