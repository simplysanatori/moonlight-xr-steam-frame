# Lane eye-swapchains: a swapchain per eye for every projection layer

Branch `lane/eye-swapchains`, from main 72c96b6. Headless only. The owner's run is the real test (last section).

## The problem

SteamVR 2.18's client library downsamples any scene texture wider or taller than 3458 px. It does it every frame, in a pass inside our process. The number is 1.6 x the panel's 2160 + 2, and it stayed 3458 with the per-app resolution at 2640 and at 2160.

Evidence, `~/.local/share/Steam/logs/xrclient_moonlight-xr.txt`, the owner's run of 2026-10-11 06:35 (line 1234 on):

```
[Info] - Setting max texture dimensions to 3458x3458 before requiring downsampling
[Info] - Scene[0:0]: 0x8c80000093f [4320x2160]
[Info] - Scene[1:0]: 0x8c80000093f [4320x2160]
```

Both eyes named the same 4320 x 2160 texture. Our own log said `curved picture: projection layer 4320x2160 (two eyes)`. Every projection layer was double wide, both eyes side by side:

- the curved and 3D picture's layer (`curveSwapchain`, 2 x recommendedEyeWidth),
- the room's layer (held at 1728 per eye at env res Standard by `roomEyeFitScene` so 2 x 1728 fitted under the limit; High and Ultra went past it),
- the controller models' layer (the room's size).

So the curved or 3D picture reached the eye at about 1729 px per eye whatever the resolution setting. The flat quad path (curve 0, 3D off) samples the full frame and was not affected. A later owner run (07:06) had the per-app resolution raised: `curved picture: projection layer 6816x3408 (two eyes)`, which SteamVR brought down to 3458 wide again, with a downsample pass over 6816 x 3408 every frame.

## The change

One projection layer a frame stays the rule (MLXR_ONE_PROJECTION, adc2c77). Each of its two views now names its own swapchain. This matches how SteamVR submits anyway: OpenVR takes a texture per eye, and the xrclient log already lists `Scene[0:n]` and `Scene[1:n]` separately.

- `xr/xr_session.c`: the curved and 3D picture's layer has `swapchain[2]`, each eyeW x eyeH, both acquired and released every frame. In 3D the warp's own image (sampled by the panel pass, never shown to SteamVR) is also one image per eye, each with its own mip chain, so the eyes no longer share a mip chain and the seam inset is not needed on this path. Eye swap picks the image per eye.
- `xr/xr_room.c`, `xr/xr_model.c`: the room's and the models' layers have a swapchain per eye. `XrCtx.roomSwapchain`, `modelSwapchain` and their images and views are arrays of two. `roomAcquired` and `modelAcquired` are a bit per eye.
- `xr/xr_world.[ch]`: `XrEyeTargets` says where the two eyes go, an image each (the session) or the two halves of one image (tests and snapshots). `xrEyesPanel`, `xrEyesBarrier`, `xrEyesWorldDepth`. The room, model, glow and picture passes draw through it. With an image per eye each pass runs once per eye with `eyes = 1`; side by side it is one pass for both, as before. The room and model depth image is eyeW x eyeH per eye, with a depth barrier between the two eyes' passes.
- `xr/xr_depth.[ch]`: `xrDepthRecordWarpEyes` warps each eye into a target of its own. `xrDepthRecordWarp` (halves of one target) wraps it.
- `xr/xr_layers.c`: `xrProjectionEye` fills a view from its eye's own swapchain (whole image, offset 0). `xrProjectionEyesOwn` checks a layer: two different swapchains, neither rect past 3458. The session logs the check once for the picture's layer.
- `rooms/room_logic.[ch]`: `ROOM_SCENE_MAX_WIDTH` (3456 for both eyes) is gone. `STEAMVR_TEXTURE_MAX` 3458 is a limit per eye. `roomEyeFitScene` (Standard to 1728) became `roomEyeFitTexture` (any tier, each eye inside 3458, aspect kept). New: `eyeScaleParse`, `pictureEyeSize`.
- `MLXR_EYE_SCALE` (float, default 1.0, clamped 0.5 to 1.6) scales the picture layer's eye: recommended size x scale, inside the runtime's maximum. It is not capped at 3458. The log says the size and whether it is within the limit:
  `curved picture: projection layer 2160x2160 per eye, a swapchain each (recommended 1728x1728, MLXR_EYE_SCALE 1.25), 3 images each; within SteamVR's 3458; swapchains alive 4 of 16`
- The video swapchain stays double wide in a stereo mode. SteamVR never sees it then (the picture goes through the projection layer), so it costs memory but is not downsampled. Its quad path with eye swap and seam inset in `xr_layers.c` is upstream's, for a runtime that shows one-eye quads; the Frame never takes it.

## Sizes per eye

The picture's layer (curve on, or 3D on; the room and the models are drawn into it then): recommended x MLXR_EYE_SCALE. At the installer's per-app 2160: 2160 per eye (was 4320 side by side, shown at about 1729). At 1.6: 3456, still within. At the owner's later 3408 recommendation: 3408, within (scale above 1.01 would go past and is logged as such).

The room's own layer (3D off, flat picture), per eye, at a 2160 recommendation:

| env res | before | now |
| --- | --- | --- |
| Low | 1080 (2160 side by side) | 1080 |
| Standard | 1728 (fitted to 3456 side by side) | 1760 |
| High | 2160 (4320 side by side, downsampled) | 2160 |
| Ultra | 2800 (5600 side by side, downsampled) | 2800 |

At a 3408 recommendation: Low 1280, Standard 1760, High 2560, Ultra 2800. The models' own layer takes the room's size.

## Swapchains alive

The stats line counts every swapchain made (`swapchains alive N of 16`). The new ones are counted: the picture's layer 2, the room's 2, the models' 2. Most at once: UI atlas 1, video 1, picture 2, room 2, models 2, glow 1, supersample 1, passthrough 1 = 11 of 16 (was 8). Headless: 4 with the picture in the void, 6 with a room and the 3D picture.

## Tests

All 135 suites pass (`ctest -E audio_pulse`; launcher_handoff and launcher_vs_stock skip without a launcher build, as on main). New and changed:

- `m5_eye_swapchains` (new): the room's and the models' layers built by `xrBuildLayers` name two different swapchains, the whole 2160 x 2160 image of each, offset 0; `xrProjectionEye` as the picture's layer uses it; `xrProjectionEyesOwn` refuses one swapchain for both eyes, the old halves of a double wide image and an eye of 3459.
- `m5_world_eye_images` (new, GPU): room, models, and curved picture with glow drawn an image per eye against the halves of one image. Max channel difference 1, 0 and 1. Each eye differs from the other eye's half, so the views are not swapped. Cost at 2160 an eye, median of 25, side by side / an image per eye: room 1.557 / 1.469 ms, models 0.037 / 0.047 ms, curved picture + glow 0.373 / 0.379 ms. Each eye is drawn once.
- `rooms_*`: the tier sizes above, `roomEyeFitTexture`, `eyeScaleParse`, `pictureEyeSize`.
- `m6_pipeline` now warps an image per eye. The M6 stereo checks on `gen_scene.sh`'s clip pass: ball shift +5.73 px (near, crossed), far wall -11 px (behind), map ball 227.5 against wall 4.1, warp both eyes 1.15 ms GPU at 1080p.
- `mlxr-world-snapshot` (in `ui-snapshots`) draws every pass an image per eye and composes the left one; the room, curved glow and one-layer pictures look as before, and its one-layer checks pass (opaque, models over 786 of 786).

## Headless runs

`moonlight-xr --file test-1080p60.mp4 --seconds 15`, own config and log directory. A headless session gets SteamVR's default recommendation of 1728 per eye (no per-app entry), so before the change the layer was 3456 wide, just under the limit. The headless runs cannot show the downsample pass itself. GPU ms from the stats line, two 5 s windows where there were two:

| case | before (3456 x 1728) | after, 1728 an eye | after, 2160 an eye (MLXR_EYE_SCALE 1.25) |
| --- | --- | --- | --- |
| 3D on, void | curve 0.09, 0.12 | curve 0.02, 0.02 | curve 0.18, 0.16 |
| curve 50, void | curve 0.11, 0.11 | curve 0.11, 0.19 | (no stats window) |
| room, 3D off | room 1.49, 1.52 | room 1.77, 1.34 | |
| room, 3D on (room in the picture) | room 1.51, 1.49; curve 1.67, 1.65 | room 1.28; curve 1.37 | room 2.27; curve 2.42 (first window, includes the room load, max 10 ms) |

The curve figure includes the room when the room is in the picture's layer. These runs are noisy: whether the controllers were awake changed between runs (models drawn or not), and some of them overlapped another live session (see risks). The offscreen GPU test above is the cleaner comparison.

SteamVR side, headless: the xrclient log still has `Setting max texture dimensions to 3458x3458 before requiring downsampling`, and it now lists a texture per eye:

```
[Info] - Scene[0:0]: 0xae70000093f [1728x1728]
[Info] - Scene[1:0]: 0xaed0000093f [1728x1728]
```

against `Scene[0:0]` and `Scene[1:0]` on one `[3456x1728]` handle before. So SteamVR took the two swapchains as the two eyes of the layer. Nothing is drawn headless, so whether the picture looks right cannot be seen from here.

## The owner's check

1. Curve on (3D off), Desktop at 2560x1440, default distance. Text should be about as sharp as the flat quad path (curve 0, 3D off), less the one resample the projection layer adds. It was clearly softer before.
2. 3D on, same. The two eyes should line up as before (no swap, no seam line down the middle of a picture).
3. A room with 3D on, and a room with 3D off: walls steady, no judder (still one projection layer a frame).
4. moonlight.log: `curved picture: projection layer WxH per eye, a swapchain each ...; within SteamVR's 3458` and `the layer's two views on two swapchains within SteamVR's 3458`. xrclient log: `Scene[0:n]` and `Scene[1:n]` on two different handles of the per-eye size.
5. Cost: the stats line's `gpu curve` and `gpu room`, and the compositor GPU ms in `vrtiming.log`, should not rise against the last run at the same resolution. With the resolution at 3408 per eye the picture layer is 2.5 x the pixels of 2160 (it was the same before, plus the downsample); if the frame is too slow there, `MLXR_EYE_SCALE=0.65` gives about 2215.

## Open risks

- Not seen in the headset: SteamVR's handling of two swapchains in one projection layer on the display, its "advanced image processing of scene textures" on per-eye textures, and timing (both images are acquired and released in the same frame, after one submission).
- The headless runs on 2026-10-11 around 06:52 to 07:06 overlapped live headset sessions (moonlight.log shows sessions that ended within a second or two of a headless run starting). A headless run can take the scene from a session in use. Check that nothing is running before a headless run.
- Per eye 3408 (the raised per-app resolution) fits the limit but is heavy for the room drawn into the picture's layer; see the owner's check, step 5.
- The video swapchain is still 2 x the picture wide in a stereo mode (about 88 MB at 2560 x 1440 with 3 images). Never shown, so no downsample; could be made picture-wide later.
