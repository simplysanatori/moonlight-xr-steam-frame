# Steam Frame controller models

The client can draw a model at each hand's grip (Settings, controller model; off by default as
upstream). On a Frame it draws the Frame's own controllers, when they have been made from the local
SteamVR install.

## Lane fix-polish: no generic controller

The owner asked for the generic Quest-style controller to go ("they're not even mapped correctly"):
`assets/models/controller.room` is deleted, Show controllers is Off / Frame / Gun (Custom from the
file or the launcher), and `controller_model = generic` (or the old number 2) reads as `frame`. The
kinds are renumbered (off 0, frame 1, gun 2, custom 3); a file's old numbers 3 and 4 still mean gun
and custom. Frame (and Gun or Custom falling back to the Frame's chain) with no converted Frame
model loads nothing and draws no controller, and the log says why once
(`controller model (frame): no model files under .../models ...`). The generic rows of the sections
below are history. The painted tests that drew it now draw a painted box pair written by the test.

## The Gun model: a G17 (lane/gun-guncon)

- **Source.** "G17 Gen 5 Handgun + Olight - NSW Police Force" by 8sianDude (Sketchfab haoliu95), CC BY 4.0,
  chosen by the owner in place of the original procedural pistol (and of the GunCon scans, which have no
  licence). Credit and the list of changes: `assets/licenses/README.md` (and CREDITS.md, README.md). The
  source (glTF + 3.6 MB bin + 20 MB of textures) is not in the repository; `tools/xr/make_gun.py` converts it:
  `python3 tools/xr/make_gun.py --in scene.gltf assets/models` (deterministic: the committed files are
  byte for byte what it writes from the download).
- **What is kept.** Nodes `g17_gen5` (frame 7642 triangles, slide 13302) and `body_low` (the light 5196, its
  lens 170); of `mag_full` (the magazine in the grip, 22470) its three largest pieces, 2840 triangles (body,
  inner wall, base plate; the 34 cartridges inside it cannot be seen). Dropped: `mag_full.001` (21390) and
  `mag_empty` (3030), the spare magazines behind the pistol. 73200 in, 29150 out a hand (28754 body + 396
  trigger), 32451 vertices; under the 30000 budget, so nothing is simplified (the tool can, with pyfqmr).
- **Pose.** Turned (+z forward, +y up in the scene), scaled 0.11176 (the light's front to the frame's back
  0.2135 m, the slide 0.186 m, 0.138 m tall: a real G17 Gen 5), the bore (the barrel's front ring centre, 9 mm
  across) on the z axis, then slid along it until the grip's centre line at mid height crosses the old
  pistol's grip line (22 degree rake through y -0.0525, z 0.043; the G17's own grip rakes about 25 degrees).
  The barrel's front is at z -0.108, the light's at -0.121, the grip's bottom at y -0.124.
- **Trigger.** The frame mesh's trigger and its safety blade (and a pin through them), split out by
  connected pieces inside a box: 396 triangles, hinged at their top front (0, -0.020, -0.013), 22 degrees back
  at a full pull, as the old pistol's.
- **Colours.** Each triangle's base colour texture sampled at five points, reduced to 16 colours (k-means in
  linear light) on a 16x1 palette: black polymer, the grey slide, the FDE magazine base plate, the light;
  the lens set pale. Normals are the model's own.
- **Cost.** 1.9 MB a hand on disk and in GPU buffers (the Frame's own textured models: 34 MB a hand), about
  twice the Frame controllers' triangles; the models pass was 0.16 to 0.21 ms for those. Not measured on
  the Frame.
- **A gun of your own.** The Gun kind reads `gun_left.model` + `gun_right.model` from the user's models
  folder first (both halves; `xr/core/xr_controller.c`), then the bundled pair, then the Frame chain;
  the log says `the gun is your own` or `the gun is the bundled one`.
- **Preview** (headless, a small software render of the MXFM files, not the client's shader): procedural
  pistol and G17 side by side from five sides with the aim ray, and the trigger at rest and pulled. Nobody
  has held it in the headset yet.

## v3: moving parts and a matte finish (lane/model-anim)

- **Animation.** `xr_model.c` reads each hand's controls once a frame (trigger, grip, thumbstick x/y and
  click, A/B/X/Y, menu/view, bumper, d-pad) from the client's own actions into `XrCtx.modelInput`
  (read only; the pointer, pad and shortcuts are untouched). `mlxr_model_pose` (rooms/room_gpu.c) turns them
  into per-part transforms with SteamVR's semantics: trigger and grip rotate about their pivot by
  `value_mapping` (the pressed override blends in over the last tenth of the pull), the bumper rotates when
  clicked, the face, menu and view buttons slide along their axis, the thumbstick tilts about its centre by
  `joystick_rotation_x/y` (clamped to the rim) and dips by `press_translate` when clicked, the d-pad tilts and
  dips toward the direction held. Parts at rest next to each other are one draw, so an untouched hand is still
  one draw. The system button is reserved by the runtime and never moves.
- **MXFM 2**: adds motion type 4 (d-pad: SteamVR's `dpad` motion, previously dropped). Version 1 files
  still load (their d-pad stays still). Rerun `install.sh` to regenerate.
- **Matte** (`shaders/model_tex.frag`): roughness lifted to a floor of 0.5 (Valve's map is glossy), key
  highlight x0.7, sky edge reflection x0.25, base colour floored at linear 0.014 so the diffuse light shows
  the shape on the near-black plastic. `MLXR_FRAME_MODEL_GAIN` still scales the colour.
- **Looking**: `mlxr-model-pose-snapshot OUTDIR FRAMEMODELSDIR` renders each hand at rest and with the controls
  pressed (three close views a row); `rooms_model_pose` checks the transforms in numbers (made-up and real
  models) and renders rest vs pressed.
- **GPU**: steady state 0.16 ms at rest (unchanged), about 0.41 ms with any part moving (the first call
  after process start reads 0.5 to 1.5 ms: cold GPU). Both hands, 2 x 1728^2, Turnip.

## v2: textured, full detail (lane/models-tex)

v1 (below, kept as a fallback) painted a ~4500-triangle decimation from vertex colours; the owner
found it "looks terrible". v2 draws SteamVR's own meshes, every triangle, with SteamVR's own
textures.

### What changed

- **Converter** (`frame-controller-models.py OUTDIR [rendermodels dir]`) writes
  `frame_controller_{left,right}.model` (MXFM version 1; the layout is in the script's docstring):
  every visible component (body, thumbstick, buttons, trigger, grip button, bumper, d-pad; the
  hidden `status` light left out), every triangle, each component its own index range, in OpenXR
  grip space through the `openxr_grip` component exactly as v1 did. Per vertex: position, normal,
  a tangent frame for the normal map, UV (V flipped: the textures are stored top row first).
  Textures as shipped (2048²): `_color` (sRGB) with `_ao` (linear) in its alpha, and `_normal`
  (green turned from DirectX to OpenGL) with `_rough` in its alpha. `_metal` is all zero on the
  Frame and is left out. Each component's SteamVR motion (translate/rotate/joystick, input path,
  pivot, axis, range) is stored for later animation. A successful run removes the v1 `.room` pair
  from OUTDIR. Pure Python + GdkPixbuf, about 1.6 s for both hands. Knobs:
  `MLXR_FRAME_MODEL_TEXTURE=N` scales the textures to N² (default: as shipped);
  `MLXR_FRAME_MODEL_TRIANGLES=N` still decimates with v1's simplifier (debugging only, default off).
- **Loader** (`rooms/room_gpu.c`): `mlxr_model_file_parse` (header, counts, sizes must add up),
  then per hand: components and indices range-checked, the mesh into the shared vertex/index
  buffers, the two images straight from the file into the staging buffer, `R8G8B8A8_SRGB` colour
  and `R8G8B8A8_UNORM` surface images with a full mip chain blitted on the GPU (linear blits: the
  sRGB image is filtered in linear light; the normals average and shorten, which the shader reads
  as roughness). Everything is read, checked and created before any command goes into the frame's
  command buffer, so a failure falls through cleanly. Order: textured pair, then the painted
  `.room` pair (v1), then the generic `controller.room`, each skip with its reason on stderr.
- **Pass** (`shaders/model_tex.{vert,frag}`, `MLXR_PASS_MODEL_TEX` in `shaders/passes.c`): the
  painted pass's key light direction (`model.vert`), plus a sky/ground fill dimmed by the AO, GGX
  highlights and a Fresnel sky reflection, so the near-black plastic shows its shape through its
  highlights as under SteamVR. Lit in the model's own space; sampled trilinear (with anisotropy)
  through the rooms' atlas sampler. The colour is decoded once by the sRGB sampler, lit in linear
  light and written linear into the sRGB swapchain (no double decode or encode).
- **Client wiring** (`xr/xr_model.c`, `xr/xr_world.c`): the textured pipeline is warmed when the
  textured pair loaded; the log says which model loaded (textured / painted / generic) with the
  texture size, levels and component counts. Two environment knobs, read once:
  `MLXR_FRAME_MODEL_GAIN` (default 1: base colour times this in linear light, at draw time, no
  reinstall needed) and `MLXR_FRAME_MODEL_VIEW` (0 lit, 1 normals, 2 lit without the normal map,
  3 base colour x AO).
- **Packaging**: `install.sh` and `tools/release.sh` clear and exclude `frame_controller_*`
  (`.room` and `.model`); `.gitignore` covers `/assets/models/frame_controller_*`. The converter
  still runs only from `install.sh`, into `DEST/assets/models/`; without `/opt/steamvr`, the
  converter or GdkPixbuf the install carries on and the generic controller is used.

### Numbers (Frame, Turnip, headless)

| | generic | painted v1 | textured v2 |
| --- | --- | --- | --- |
| triangles (left / right) | 1592 (one mesh) | 4204 / 4822 | 14886 / 16240 (all) |
| vertices (both hands) | 890 | 8475 | 19525 |
| textures | none | none | 2 x 2048² RGBA8 per hand, 12 mips |
| GPU memory | 57 KB | 470 KB | 87 MB (23 MB at 1024²) |
| file per hand | | ~0.2 MB | 34 MB (9 MB at 1024²) |
| load (record, warm cache) | 0.2 ms | 0.3 ms | 37 to 40 ms (11 ms at 1024²) |
| models pass, both hands, 2 x 1728² | 0.026 to 0.031 ms | 0.040 to 0.053 ms | 0.159 to 0.209 ms |

GPU time from `mlxr-model-snapshot` (timestamp queries round the pass, median of 25, four runs).
The pass is about 2% of the 11.1 ms frame at 90 Hz, well under the 1 ms budget. The load is a
one-time stall on the render thread the first frame the model switch is on (the loader was already
synchronous for v1): at 2048² about 4 frames at 90 Hz from a warm page cache, more from a cold one.

### Headless verification (what was looked at)

- `mlxr-model-snapshot OUT DIR` (generic, painted and textured rows; front, +x, above and
  three-quarters behind; each hand): the textured models sit exactly where the painted v1 models
  sit (same grip transform, same boxes: left x -0.022..0.050, right x -0.050..0.022,
  y -0.050..0.020, z -0.084..0.054 m), so the hold angle is v1's.
- `MLXR_FRAME_MODEL_VIEW=3` (base colour x AO, brightened crop): the button legends read correctly,
  upright and not mirrored (Y top, A bottom, X left, B right, Steam logo, menu button), so the UVs
  and the V flip are right. `MLXR_FRAME_MODEL_VIEW=1`: the face plate's normals point up, the sides
  out, the normal map's bumps (buttons, stick ring) show; front faces are not lit inside out.
- `rooms_model_textured` checks the colour path in numbers: an sRGB 200 texel renders as 200 (a
  double decode would give about 150), and the 1x1 mip of four quadrants is their linear-light
  average (160 against 159; averaging the bytes would give 122).
- All of it is headless. Nobody has seen v2 in the headset yet.

### Tests

- `frame_models_converter` (62 checks): MXFM parse rules, every triangle, components in order, the
  grip transform, unit normals and tangent frames, V from the top row, the textures (colour + AO,
  normal with its green turned + roughness, flat stand-ins for missing optional maps, the size
  knob), the v1 pair removed, bad sources write nothing; the real models when SteamVR is here.
- `rooms_frame_models_convert` converts the real models into the build tree (skipped without
  SteamVR); `rooms_model_textured` (37 checks): parse rules, load, the GPU's buffers read back
  equal to the files, absolute component ranges, the mip chain, the colour/UV render check, the lit
  render, the gain; then renders the real models (`<build>/rooms/tests/out/model_textured_frame*.png`).
- `rooms_model_fallback` (26 checks): nine cases of missing, cut-short and wrong-version files,
  textured, then painted, then generic, then no model.
- `m5_world_frame_models`: the textured pair through `xrModelRecord` (the client's own call),
  preferred over the painted pair, not mirrored, back to the painted pair when a half does not
  parse.

### What the owner must check in the headset

1. **Hold angle**: the controller in the hand where the real one is (handle along the palm,
   trigger under the index finger). Unchanged from v1, which was never confirmed either.
2. **Colour and brightness**: the plastic is drawn near black, as Valve's texture has it, its
   shape in grey highlights. If it reads too dark in a dark room, try `MLXR_FRAME_MODEL_GAIN=3`
   (then 6) in the launch environment; no reinstall needed. The light is fixed in the room (key
   from above and behind), not taken from the room's own lights.
3. **Shimmer**: specular aliasing on edges while moving (the shader widens the roughness by the
   mips and by the normal's change across the pixel; the model layer has no MSAA).
4. **The one-time stall** when the model switch goes on (about 40 ms). If it shows, reinstall with
   `MLXR_FRAME_MODEL_TEXTURE=1024` (11 ms, 23 MB, a little softer up close).

### Left

- (Animation: done in v3 above.)
- Loading off the render thread, if the stall matters.

## v1: painted (still read when there is no `.model` pair)

- The previous converter wrote `frame_controller_left.room` and `frame_controller_right.room`
  (MXR3, one painted part): components in grip space, simplified to about 4000 triangles a hand,
  vertex colours from the colour texture times AO. Its `MLXR_FRAME_MODEL_GAIN` was a converter-time
  colour gain; in v2 the same name is the client's draw-time gain for the textured pair.
- `mlxr_model_record` still loads such a pair when the textured pair is missing or broken, so an
  install made by the old converter keeps working until the next `install.sh`.

## How to look

`ninja -C build mlxr-model-snapshot` then
`env -u LD_LIBRARY_PATH build/xr/tests_m5/mlxr-model-snapshot OUT DIR_WITH_FRAME_MODELS` renders
whichever of generic, painted (`.room`) and textured (`.model`) are there at the same grips and
times the pass; with `MLXR_FRAME_MODEL_VIEW=1` or `3` the sheet is
`models_generic_vs_frame_viewN.png`. Converted files and renders are made from Valve's models: keep
them in the build tree or /tmp, never in git.
