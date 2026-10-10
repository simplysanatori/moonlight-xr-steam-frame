# Steam Frame controller models

The client can draw a model at each hand's grip (Settings, controller model; off by default as
upstream). It ships one generic Quest-style controller. On a Frame it now draws the Frame's own
controllers instead, when they have been made from the local SteamVR install.

## What

- `frame-controller-models.py OUTDIR [rendermodels dir]` reads SteamVR's `frame_controller` driver
  render models (`/opt/steamvr/drivers/frame_controller/resources/rendermodels`, OBJ + JSON +
  colour/AO PNG) and writes `frame_controller_left.room` and `frame_controller_right.room`
  (the `.room` MXR3 format, one part painted from vertex colours, like `controller.room`).
- Per hand: every visible component (the hidden `status` light is left out) is put in OpenXR grip
  space with the JSON's `openxr_grip` component (origin and `rotate_xyz`, as the owner's fork does),
  simplified with the fork's pure-Python quadric collapse (seam, open-edge and non-manifold
  vertices never move) toward `MLXR_FRAME_MODEL_TRIANGLES` (default 4000), then each vertex gets the
  colour texture times the AO map: the average of the texture under the vertex's hat function over
  the triangles around it in its own UV chart (sub-triangle samples, bilinear, linear light), so a
  texture seam keeps its two sides apart.
- `MLXR_FRAME_MODEL_GAIN` (default 1) multiplies that colour in linear light. Valve's plastic is
  near black (about sRGB 10 of 255) and this client has one diffuse light and no gloss, so at 1 the
  controllers read as silhouettes in a dark room. 8 to 12 gives dark grey. Tune on the headset.
- Python 3 stdlib and GdkPixbuf only. Run with `python3 -I`. Takes about 3 s. Both hands are
  converted before either file is written: a bad source writes nothing.
- Valve's files and anything derived from them are never in git (`.gitignore`) or in the release
  tarball. `packaging/install.sh` runs the converter into `DEST/assets/models/` on every install
  (source checkout and unpacked release alike; `tools/release.sh` ships the converter as
  `frame-controller-models.py` next to `install.sh`, strips any model from the tarball and verifies
  that). No SteamVR directory, no converter or no GdkPixbuf: a clear message, the install carries on,
  the generic controller stays. `MLXR_FRAME_RENDERMODELS` points the install at another source dir.
- Loader (`rooms/room_gpu.c`, `mlxr_model_record`): both Frame files present and parsing give one
  pair of buffers, left then right, with per-hand first index, index count and vertex offset;
  anything else (one missing, truncated, not a mesh) falls back to `controller.room`, as before,
  with the reason on stderr. The Frame models are not mirrored; the generic one still is for the
  left hand. `MlxrModelParams` gained `perHand` and `handFirstIndex/handIndexCount/handVertexOffset`
  (zeroed params draw exactly as before). `xr_world.c` logs which model loaded and from where.

## Numbers (Frame, Turnip, 2 x 1728x1728, both hands)

| | generic | Frame |
| --- | --- | --- |
| triangles | 1592 each hand (one mesh) | left 4204, right 4822 |
| vertices | 890 | left 3904, right 4571 |
| GPU memory | 57 KB | 470 KB |
| models pass | 0.03 to 0.10 ms | 0.15 ms |

The generic figure moves with the GPU clock; the pass is small either way. The grip-space boxes are
about 7 x 7 x 14 cm (left x -0.022..0.050, right x -0.050..0.022, y -0.049..0.020,
z -0.084..0.054 m): the right hand's thumbstick leans to -x, the left's to +x.

## How to look

`ninja -C build mlxr-model-snapshot` then
`env -u LD_LIBRARY_PATH build/xr/tests_m5/mlxr-model-snapshot OUT DIR_WITH_FRAME_ROOMS`
renders generic and Frame at the same grips (identity orientation, 0.4 m ahead) from the user's
side, from +x and from above, one hand at a time, into `OUT/models_generic_vs_frame.png`, and
times the pass. Tests: `frame_models_converter` (made-up render models: parse, budget, the grip
transform, colours = texture times AO, both-or-neither on a bad source; the real models too when
SteamVR is here) and `m5_world_frame_models` (loader: pair, ranges, not mirrored, each fallback).

## Things to know

- Trigger forward (-z) and the face up (+y) match the generic controller, and each model is the
  right hand. The handle differs: the generic one is drawn as a vertical handle, SteamVR's grip has
  the Frame's handle running back along +z (the aim pose is pitched about 43 degrees from the grip
  in the JSON). That is what the SteamVR runtime reports the grip as; it was only checked headless
  here, not against a held controller.
- Seams leave the right hand a little over the budget (4822), as in the fork.

## Left

- Animating the trigger, grip, sticks and buttons from the JSON motions (the fork does; this does
  not, all components are static).
- A settled default for `MLXR_FRAME_MODEL_GAIN` after seeing it in the headset.
- Headset check of the placement in the hand.
