# M5 report: rooms, glow, controller models, click, supersampling (lane/m5)

Moonlight XR's `xr_room.c`, `xr_model.c` and `xr_ambilight.c` (Steam Frame fork, Gilleece, GPLv3) ported onto the native `XrCtx`, on top of lane R's `rooms/room_gpu` + `room_logic` and lane S's passes. `xr/xr_later.c` (the M4 stand-ins) is gone: every row and picker cell is live. Built on main 0ec47c6, with main 9a56f6d (keyboard lane) merged in. Nothing here has been seen in the headset. SteamVR keeps a session started over SSH SYNCHRONIZED, so only the first frame or two render. The GPU parts were proven headless on the Frame's Turnip in `tests/m5`, and the snapshot tool composes them into pictures.

## Ported (logic kept; every change marked `native` in the code)

| File | Upstream lines | Native lines | What changed |
| --- | --- | --- | --- |
| `xr/xr_room.c` | 1172 | 695 | Kept as upstream: roomEffective, roomLevelsFromTable, roomScreenPercent, roomGlowOn, roomSizeDragging, roomScale, roomDim, **applyRoomPlacement** (including the hold and restore of the user's own placement), roomAssetsReady, the bookkeeping of uploadRoomGeometry, buildableRoomStyle, initRoom, locateRoomViews, prepareRoom, renderRoom, nativeSetRoomLevels (now `xrSetRoomLevels`). The statics upstream kept here (table, roomParams, roomFarPlane, vertex build, dither, spill) are room_logic's, under the same names. **Native:** a loader thread reads the model and the ASTC atlas (`mlxr_room_record`) into its own command buffer. The host submits that buffer ahead of the frame, so "assets arrived" means "that room is resident". A replaced room is freed only once the GPU has passed the last frame that drew it. A rescale also waits for the GPU. The draw is `mlxr_pass_room` into the swapchain image. The clear colour (gamma encoded upstream) is decoded for the `_SRGB` target. Debug properties became `MLXR_ROOM`, `MLXR_ROOM_SCALE` and `MLXR_ROOM_DIM`. |
| `xr/xr_model.c` | 365 | 203 | updateControllerModels unchanged. initModelPass, locateModelViews and renderControllerModels keep upstream's order. **Native:** the draw is `mlxr_pass_model`, using the depth image the room uses. The model loads the first frame the switch is on. GL timer queries became the host's timestamps. |
| `xr/xr_ambilight.c` | 471 | 399 | ambiEffective, ambiBarEdge, detectAmbiBars, and the rules and order of runFrameColorSample, runAmbiBarDetect, finishAmbiBarDetect, runGlowEdge, runGlowRender, glowStale and redrawGlow are all upstream's. **Native:** `mlxr_pass_ambi`, `_glow_edge` and `_glow` write into app-owned images. The frame arrives as an `MlxrVideo` (hardware NV12 import or software planes). The glow goes into an app `_UNORM` image, which is then copied into a 256² quad swapchain, or drawn on the curved picture's surface. The detector reads back through a host-visible buffer one frame later. Rows run top first, and the bar log line says which edge is which. |
| `xr/xr_world.c/.h` | new | 765 | The GPU glue GL used to hide: app images, `createArtSwapchain`, the shared D32 depth, uploads submitted ahead of the frame, rooms retired by serial, swapchain images released after the submit, GPU timestamps for each section (warp, glow, room, models, curve) in the 5 s stats line, the curved glow (`mlxr_pass_panel`, upstream's glow cylinder shape from `glowCylinderFor`), the click hook, and finding the assets (`./assets`, then the binary's directory, then the source tree). |
| `xr/xr_layers.c` | — | +120 | addRoomLayer, addGlowLayer and addModelLayer as upstream, in upstream's order (passthrough, room, glow, picture, …, keyboard, models, pointer, toast) and drop order (toast, models, hover ring, cog button, clock). A curved picture's glow lives inside the curved layer, because there are no cylinder layers. The supersampled picture's quad is added. The keyboard's Full chip gets a hover ring (orchestrator's request). |
| `xr/xr_session.c` | — | +330 | Calls the world each frame: FrameBegin, the colour sample and glow after each new warp (or a still-picture glow redraw), updateControllerModels, the room every display frame, the models, the curve (glow first), the flat glow copy, uploads ahead of the frame in the same submission, then releases. Also: the click thread, the environment picks for rooms, `pi.atlasAnisotropy = 4`, `maxEyeWidth/Height`, supersampling (below). |
| `xr/xr_prefs.c`, `xr_panels.c`, `xr_assets.c`, `xr_renderer.h`, `xr_native.h` | — | — | Upstream's nativeInit defaults (ambiSmooth 0.08, glowNorm, bar detection, room overrides, envResTier). All 5 picker cells are live. Environment ids map to cells both ways. The `SETTING_ROOM_*` rows are stored under the room showing. `xrRowLater` greys nothing. `UiView.room_env` is set for the Room tab, and the cog panel's cache key includes it. Upstream's XrCtx fields are back with Vulkan types. `roomStyleForCell` comes from room_logic. |

### Fixed on the way: the curved picture was too dark (M4 bug)
The curved picture's own image was `_SRGB`. The sampler decoded it, and then the panel pass decoded the gamma again. It is now `_UNORM`. `m5_world_curve` measures this: flat 217 26 26, curved through UNORM 217 26 26 (max difference 1), the M4 path 177 3 3 (max difference 70).

## Swapchain and layer budget (SteamVR: 16 swapchains, 16 layers)

| Swapchain | Size | When it is made |
| --- | --- | --- |
| picture (video) | stream size | first decoded frame |
| UI atlas | 2048x2960 | always |
| passthrough's transparent projection | 16x16 | first time passthrough is on |
| curved picture's projection | 2x1728 x 1728 | first time the curve is above 0 |
| supersampled picture | footprint (e.g. 928x522) | Supersample on and the picture minified 1.3x or more |
| room projection | 2x1728 x 1728 (env res standard) | first frame a room is picked |
| controller models' projection | 2x1728 x 1728 | first frame a model shows |
| glow quad | 256x256 | first flat glow |

That is 8 of 16 at most. Each one is logged as "swapchains alive N" when it is made, and the count also appears in the 5 s stats line. Memory: room 3x24 MB, models 3x24 MB, shared depth 22.8 MB, room assets 11–12 MB (one resident).

Layers, counted by `m5_budget` (busiest frames, toast included):

| Frame | Layers |
| --- | --- |
| void, head locked, full bar (pill + 7 buttons), glow, models, stats, ray, dot, toast | 15 |
| passthrough, head locked, bar, glow, stats, ray, dot, toast (no models) | 15 |
| void, curved (glow inside the curve's layer), bar, models, ray, dot | 14 |
| room, bar (no pill in a room), glow, models, stats, ray, dot, toast | 14 |
| room, Room tab or Display tab, glow, models, stats, ray, dot, toast | 11 |
| room, keyboard, glow, models, stats, ray, dot | 10 |

The worst case is 15 of 16. If a runtime had fewer layers, the drop order is checked: the toast goes, then the models.

## Display tab, Room tab, picker

| Row / cell | State |
| --- | --- |
| Display: Sharpen, Stats, Head locked, Head aim, Controllers, Pointer sleep, Ray | as M4/M3 |
| Display: Supersample Off/Normal/Quality | **live**: app-side pass (2 or 4 taps) into a footprint-sized swapchain when the picture is minified ≥1.3x. Default Off. Needs a headset A/B. |
| Display: Controller model | **live**: generic controller (the fork ships no Frame controller model as a plain asset: only `controller.room`, same as ours). Default Off. |
| Display: Click sound | **live**: `audio/click.c`, started off the frame loop the first time it is on; one click on each panel press (`IN_CLICK`). Default On. |
| Display: Glow, Glow level | **live**; in a room this is the room's own glow switch (upstream) |
| Display: Screen light | **live**: the picture's light washes over the room |
| Room tab (screen tab's place in a room): Brightness, Glow, Screen light, Light level, Size | **live**, saved per room in `[rooms]`. Size is greyed in Grand Cinema (fixed room, upstream). |
| Picker: Passthrough, Black void, Home Theater, Grand Cinema, Synthwave | **all live**, saved as `environment` |
| 3D tab | greyed until M6 |

## GPU time (from timestamps, `m5_world_timing`, median of 25, Turnip Adreno 750, headless, clocks not pinned)

| Pass | ms |
| --- | --- |
| room, home theater, 2 x 1728², lit | 0.84 |
| room, grand cinema | 1.28 |
| room, synthwave | 0.82 |
| controller models, both hands, 2 x 1728² | 0.03 |
| colour sample + glow edge + glow (1080p in) | 0.27 |
| curved picture + curved glow, 2 x 1728² | 0.25 |
| warp 1080p with Quality sharpen (for comparison) | 1.60 |

The room is redrawn every display frame while one is up. Upstream drew it with each new picture, but SteamVR only turns a projection layer with the head, so a room drawn only with new pictures would swim on head translation. At about 1 ms that is affordable. In the headset the stats line prints `gpu warp/glow/room/models/curve` averages every 5 s.

## Supersampling: the numbers (`m5_world_supersample`)
At the default placement (3 m wide, 3 m away; 1728 px across about 100°), a 2560x1440 picture covers about 918 px, so the compositor minifies it 2.79x. The test models a compositor that samples bilinearly from a single-level image (our swapchain has no mips) at a sub-pixel phase. RMS error against the ideal (area-averaged) display pixels, on a 0..255 scale:

| | zone plate | desktop text |
| --- | --- | --- |
| bilinear only (no supersample) | 48.3 | 42.0 |
| supersample pass, 2 taps (Normal), then bilinear | 21.3 | 26.0 |
| supersample pass, 4 taps (Quality), then bilinear | 20.2 | 24.7 |

So the row is live. If SteamVR filters quad layers better than plain bilinear, the gain is smaller. The default stays Off, and the owner judges it in the headset.

## Headless evidence
- ctest: **100 of 100** pass (`-E audio_pulse`). That is main's 90 plus 10 new tests: `m5_room` (247 checks), `m5_env` (624 checks: every ordered pair of environments, both ways, checking blend mode, room/model layers and placement), `m5_budget`, `m5_click`, `m5_world_leaks` / `timing` / `glow` / `curve` / `supersample`, `m5_world_snapshot`.
- Leaks: Vulkan image, view, buffer, allocation and command-pool calls are wrapped. Over 3 rounds of theater → cinema → synthwave → void → passthrough, the counts stay identical. A load abandoned mid-way, a rescale and the model load leave nothing behind. After `xrWorldDestroy` the counts are back exactly to where they started. 8 rooms freed, 0 waiting.
- Glow: a letterboxed (15% bars) red/blue picture gives 4 rows of bar on each end, no columns, crop height 0.75. The glow is red on the left and blue on the right. A level change redraws it on a still picture.
- Snapshots: `ninja -C build-m5 ui-snapshots` now also writes `m5_void_glow_models`, `m5_curved_glow`, `m5_room_{theater,cinema,synthwave}` (and `_bar`, `_roomtab`), `m5_picker_rooms`, `m5_display_tab`. I looked at them: the theater is lit pink by the picture with the speakers and the TV stand; the cinema picture fills the proscenium, with the bar and no pill; synthwave has the sun, grid and Room tab; the picker's tiles are all live; the Display rows are live; the glow and the controllers look right.
- `--file test-1080p60.mp4 --seconds 8–12`, using scratch configs (`MLXR_CONFIG_DIR`, `MLXR_LOG_DIR`) for the void, home theater, synthwave, passthrough, curve 50 and supersample quality with models on: all runs exit cleanly (rc 0). The rooms load off the frame loop; the room, video and depth swapchains are made and logged; the loader thread joins at exit. The only W line is the existing `display rate N Hz refused` (SteamVR picks its own rate, as in M4). The kernel log over the runs has no iris/msm/smmu/hangcheck/GPU lines (only the camera ISP's usual CSID lines). `tools/ldd-audit.sh`: ok.

## Notes for the orchestrator
- `tests/m4/scene.[ch]` gained two hooks (projection layers, other images), and `tests/m4/CMakeLists.txt`'s `ui-snapshots` runs `mlxr-world-snapshot` too. `tests/m5` is added from `xr/CMakeLists.txt`.
- `app/main.c` needs nothing. The click is opened by `xr_session.c` itself; it uses PulseAudio's shared context, and `mlxr_xr` links `mlxr_audio`.
- `app/main.c` still logs "moonlight-xr (M4) starting". The About tab says 0.5 (M5).
- Not done: the depth model and 3D (M6). The room's Environment Res tier comes from config.ini `env_res` (no in-VR row, as upstream). There are no Frame controller meshes.

## Headset test script (owner)
1. Launch from Steam, start Desktop (or any app). Display tab: Controller model **On**. Both controllers appear in your hands and follow them. Put one in front of the picture: it draws over it.
2. Environment button → **Home Theater**. After a moment, a room appears around you with the picture on the front wall (2.88 m). Move your head side to side: the walls stay put, with no swimming. Play something colourful: the walls pick up its colour.
3. Cog → the first tab is now **Room**. Drag Brightness (the room dims or brightens), switch Glow and Screen light, drag Light level (the wash gets stronger or weaker), drag Size (the picture shrinks about its centre). Leave and come back to the room: the values are kept.
4. Picker → **Grand Cinema** (huge picture, no glow, Size greyed), then **Synthwave** (sun and grid, picture in the open).
5. Picker → **Black void**: the picture is back exactly where you had it before the rooms. Then **Passthrough**: your real room shows, and the controllers are not drawn. Then back to a room: passthrough goes away cleanly.
6. In the void: Display → Glow On, then drag Glow level on a still desktop: the glow around the picture changes at once. Play a letterboxed video: within about 2 s the glow takes its colour from the picture, not the black bars.
7. Screen tab → Curve up: the picture bends and the glow bends with it, behind it. The curved picture should now be as bright as the flat one (M4 bug fixed).
8. Click sound On: each panel press ticks. Off: silent.
9. Supersample Off / Normal / Quality on fine desktop text at the default distance: look for less shimmer on text and edges as you move your head. Report whether it is better or only softer.
10. Send `~/Games/MoonlightXRNative/moonlight.log`. The 5 s lines show `room N, glow up/off, models up/off`, swapchains (8 of 16 at most) and `gpu warp/glow/room/models/curve` ms.
