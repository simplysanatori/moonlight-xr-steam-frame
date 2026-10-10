# rooms/ report (lane R, branch lane/rooms, M5 groundwork)

Library `mlxr_rooms` (static, C11, `-Werror`): `room_logic.[ch]` (pure, from upstream xr_room.c) and `room_gpu.[ch]` (Vulkan loading of the 3 baked rooms and the controller model). Test target `mlxr-room-test`, 8 ctest entries `rooms_*` (all pass, 2 s total on the Frame). Links `mlxr_xrcore` and `mlxr_passes`; no OpenXR in it, no XrCtx.

## API (rooms/room_gpu.h)
```c
MlxrGpu g = { phys, device, queueFamily, queue };          // queue only used by the *_load sync helpers
MlxrRoomLoadInfo li = { assetsDir, ROOM_STYLE_THEATER, scale /*0 = as built*/, preferLowAtlas /*0 on the Frame*/ };
mlxr_room_load(&g, &li, &room);                            // sync: own cmd pool, submit, wait, staging freed
mlxr_room_record(&g, &li, cmd, &room); ... mlxr_room_upload_done(&g, room);   // or record into your cmd and free staging later
mlxr_room_fill(room, &roomParams);                         // vertices, indices, parts, partCount
mlxr_room_rescale(&g, room, scale, cmd);                   // debug roomscale property: rebuilds the vertex buffer from the kept model vertices
mlxr_room_unload(&g, room);                                // GPU must be idle of it
mlxr_model_load / _record / _upload_done / _fill / _unload // controller model: fills MlxrModelParams.vertices/indices/indexCount
```
`MlxrRoom` is public: `params` (RoomParams), `farZ`, `reach`, `clear[3]`, `spillGain`, `texMix`, `dim` (the XrCtx roomFarZ/roomClear/roomSpillGain/roomTexMix/roomDim upstream sets in uploadRoomGeometry), `parts[]` (the MlxrRoomPart array exactly as drawRoomEyes walks them: textured part = its atlas view + texMix 1, painted part = NULL view + texMix 0), `atlas[]` (image, view, format, levels, gpuBytes), and the stats below.

What the loader does: roomMeshParse + atlasParse the files; vertices go through roomBuildVertices (anchor, scale, spill light, dither: the 10-float layout) into one staging buffer with the indices and every atlas level (each level at a 16 byte aligned offset, which the .atlas file layout does not give: levels sit at 28+n*4 mod 16); vertex+index buffers share one device-local allocation (usage VERTEX|INDEX|TRANSFER_DST|TRANSFER_SRC); one VkImage per atlas with its full mip chain (usage SAMPLED|TRANSFER_DST), a 2D view over all levels, barriers to VERTEX_INPUT / SHADER_READ_ONLY_OPTIMAL recorded into the caller's cmd. The controller model is the file's vertices as they are (11 floats), painted from colours; a model asking for atlases is refused like upstream.

## Formats chosen
- Atlas: `VK_FORMAT_ASTC_6x6_UNORM_BLOCK` (all shipped atlases are 6x6; 4x4 and 8x8 map the same way), UNORM not SRGB (upstream samples the gamma encoded values and the shader says so). Checked per atlas with vkGetPhysicalDeviceFormatProperties (SAMPLED + FILTER_LINEAR + TRANSFER_DST, optimal tiling) and vkGetPhysicalDeviceImageFormatProperties (extent, mip levels). The device needs `textureCompressionASTC_LDR` enabled (xr_vk.c does).
- Full size atlas (4096x4096, 13 levels) is chosen first. The `_lo_` one (2048, 12 levels) only if `preferLowAtlas`, or if the full one is missing/unsupported/too large. A failure of both fails the load (upstream would draw a white room; failing is more honest, M5 can fall back to "no room").
- Depth for the room/model passes: tested with D32_SFLOAT (probe: swapchain depth formats include D32/D24S8; the depth image is private so any supported format works).
- Vertex layouts: room `MLXR_ROOM_VERTEX_FLOATS` 10, model `MLXR_MODEL_VERTEX_FLOATS` 11, as shaders/passes.h.

## Memory and load time (Turnip Adreno 750, nice'd, 5 CPUs shared with other builds)
| room | verts / indices / parts | vertex | index | atlas (4096, ASTC 6x6, 13 mips) | total GPU | load | warm reload | _lo_ total / load |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| home_theater | 16821 / 17628 / 1 | 0.64 MB | 0.07 MB | 10.26 MB (file 9.50 MB) | 10.97 MB | 9.3 ms | 5.5 ms | 3.58 MB / 2.3 ms |
| grand_cinema | 43670 / 53370 / 1 | 1.67 MB | 0.20 MB | 10.26 MB | 12.13 MB | 7.2 ms | 6.6 ms | 4.74 MB / 3.1 ms |
| synthwave | 26169 / 38874 / 2 | 1.00 MB | 0.15 MB | 10.26 MB | 11.41 MB | 7.2 ms | 7.3 ms | 4.02 MB / 3.5 ms |
| controller model | 890 / 1592 tris | 0.04 MB | | | 0.056 MB | 0.6 ms | | |
Load time is wall clock for the whole sync call (file reads, parse, vertex build, staging copy, GPU copy and wait). Staging is another ~12 MB host memory while uploading, freed at once. So a room is ~11-12 MB: no reason to ever use the _lo_ set on the Frame (it could be dropped from the repo: ~7.5 MB). A room switch (unload + load) is ~10 ms and can run on the frame thread between frames.

## Images checked (build-rooms/rooms/tests/out/*.png, 2 eyes x 1280x1280 side by side; the test hangs a colour bar picture where roomPlacement puts it)
- `room_home_theater.png`: blue-grey walls, brown carpet, two tower speakers, subwoofer, TV stand, wall panels, black screen recess on the front wall with the picture in front of it; matches the thumb's palette and furniture (thumb is taken from behind the sofa, ours from the seat).
- `room_grand_cinema.png`: coffered ceiling, red carpet and recliners, sconces, curtains/slats around the screen, picture filling the proscenium; matches the thumb (red auditorium).
- `room_synthwave.png`: neon grid ground, dark purple sky, striped sun, distant mountains; matches the thumb (the sun is at the left because we look down -Z, the thumb is taken from another yaw). The sky shows faint vertical banding in the atlas itself.
- `overlay_home_theater.png`: the model pass over the room pass, premultiplied over (this is what the two projection layers do); `model_hands.png`: both controllers, left mirrored, shaded.
- `*_unlit.png`: same with the picture light off.
No validation layer is installed, so none of this has been run under validation.

## Checks that run (rooms_*)
logic (60): table values, params at scale 1 and 2, levels, clamps, placement for 16:9/4:3/21:9/very tall pictures per room, far plane, dim/spill gain, dither bounds, spill weight, vertex layout, eye size tiers, view projection. load (65): file vs upload (counts, parts, readback of the vertex and index buffers bit-equal to the CPU build, finite, indices in range), atlas format/size/levels (full chain to 1x1), memory sanity, `_lo_` fallback, bad dir and style 0 refused, controller buffers equal the file. rescale: vertex buffer rebuilt at scale 2 equals the CPU build. render_*: depth all finite in [0,1] and coverage 1.000, something near, brightness sane, eyes differ (parallax), the room's wall is where the picture hangs (picture centre and top-left bar in view; the wall is 0.17 m behind the picture in the theater, 0.58 m in the cinema because the model has a screen recess; far in synthwave), lit adds light and unlit is darker, mip consistency (a 128 px render vs a box filtered 1024 px render at dim 1: mean abs difference 0.03..0.5 of 255, so the small mips are present and right). model: alpha 0/1 only, depth exactly where the model is, nothing outside, shaded, both hands. overlay: models over the room.

## room_logic.c: names and what is not here
Same names as upstream where upstream took no XrCtx: roomParams, roomResizable, roomFarPlane, roomDither, roomSpillWeight, roomWriteVertex, roomStyleForCell, roomCellForStyle, bakedRoomStyle, roomModel (table `ROOM_MODELS`), the RoomParams/RoomModel structs and the ROOM_*/ENV_RES constants of xr_renderer.h. buildModelRoomVertices is `roomBuildVertices(params, scale, src, count, out)`. Pure cores of the XrCtx functions, under suffixed names so the M5 wrappers can keep upstream's: roomEffectiveStyle, roomScreenPercentOf, roomGlowOf, roomLevelsDefault (+ roomLevelsSet = nativeSetRoomLevels' clamps), roomScaleFor, roomDimFor, roomSpillGainFor (the `lit ? gain*level` expression in drawRoomEyes), roomPlacement (the whole geometry of applyRoomPlacement), roomEyeSize (worldEyeSize), roomEyeViewProj (the per eye matrix in drawRoomEyes). M5's xr_room.c should delete its static copies of those and include room_logic.h.

## What M5 must wire into the native XrCtx (xr_later.c stand-ins)
- `roomEffective(ctx)` = roomEffectiveStyle(ctx->roomOverride, ctx->roomStyle). `roomResizable` = as is. `roomLevelsFromTable(ctx)` = roomLevelsDefault into ctx->roomBrightness/roomGlow/roomLightLevel/roomScreen (or hold a RoomLevels in ctx). `roomScreenPercent(ctx, s)` = roomScreenPercentOf(ctx->roomScreen[s], s). `roomGlowOn` = roomGlowOf(s, ctx->roomGlow[s], ctx->ambilightOn). `roomScale(ctx,s)` = roomScaleFor(ctx->roomScaleOverride, s). `roomDim(ctx)` = roomDimFor(ctx->roomDimOverride, ctx->roomBuiltStyle, ctx->roomBrightness, ctx->roomDim).
- `applyRoomPlacement(ctx, style, aspect, reseeded)`: keep the savedScreenPose/Width/Radius hold-and-restore state machine, GRAB_* resets, recentredInRoom/poseDirty and the log line on the XrCtx; take the placement from roomPlacement(style, scale, aspect, roomScreenPercent) -> screenPose {x, y, z}, orientation identity, screenWidth = width. roomSizeDragging(ctx) reads grabMode/cog drag state.
- `worldEyeSize(ctx)` = roomEyeSize(ctx->envResTier, recommendedEyeWidth/Height, maxEyeWidth/Height).
- initRoom / prepareRoom / renderRoom / drawRoomEyes: replace the GL parts. initRoom: create the swapchain and the depth image, `mlxr_passes_warm(MLXR_PASS_ROOM, 0, colorFormat, depthFormat)`. prepareRoom: on a wanted-style or scale change `mlxr_room_unload` (after the frames in flight are done with it) + `mlxr_room_load`, or `mlxr_room_rescale`. renderRoom: xrLocateViews (locateRoomViews stays), acquire/wait the swapchain image, build MlxrRoomParams: `mlxr_room_fill`, ambi = the 32x32 ambi view, eyes 2, viewProj[e] = roomEyeViewProj(fov, pose, room->farZ), eyeRect halves, spillGain = roomSpillGainFor(ambiSeeded && roomLightOn, builtStyle, room->spillGain, lightLevel), dim = roomDim, depth view; clear colour room->clear. The native ambi image must be in SHADER_READ_ONLY and the colour image in COLOR_ATTACHMENT before the pass (mlxr_barrier).
- `updateControllerModels(ctx)` (xrSyncActions, grip pose via xrLocateSpace in localSpace, controllerModelShown) and renderControllerModels: unchanged logic; fill MlxrModelParams with mlxr_model_fill, shown[], model[h] = controllerModelMatrix(grip, h == HAND_LEFT), viewProj via roomEyeViewProj with ROOM_FAR_MIN_M (upstream's model far plane).
- nativeUploadRoomModel/Atlas/ControllerModel and the Java asset reading disappear: `assetsDir` is the install's assets dir.
- Clear colour: the passes treat t->clearColor as linear on an *_SRGB target. upstream's roomClear (0.010, 0.010, 0.012) is gamma encoded; the test converts with srgbToLinear. M5 should do the same (or the difference is 0.1 vs 0.01 grey).

## Swapchain plan
Keep upstream's arrangement: one double-wide colour swapchain for the room (2*eyeW x eyeH, e.g. 3456x1728 at the recommended 1728, R8G8B8A8_SRGB, 3 images ~ 24 MB each) and one for the controller models, each submitted as one XrCompositionLayerProjection whose two views use the same swapchain with imageRect halves (allowed by the spec; no per eye arrays needed, and mlxr_pass_room/model draw "both eyes into one image" with eyeRect). That is 2 swapchains / 2 projection layers for rooms+models, well inside SteamVR's 16 layer cap (the rest being video, glow, panels, notices). The depth buffer is not a swapchain: one D32 image of the double-wide size (~24 MB) can be shared by the room pass and the model pass because both clear it and run sequentially in the frame's command buffer. Memory for rooms+models at 1728 standard: 2 x 3 x 24 MB colour + 24 MB depth + 12 MB room = ~180 MB; "low" tier (864) is a quarter of that. Two single-eye swapchains would only add a second swapchain and layer pair for nothing. Room is redrawn per video frame in upstream (the compositor reprojects between); the model per display frame; shaders/REPORT.md discusses that cadence.

## Open questions
- Frame controller model: assets/models/controller.room is the generic Quest style controller with a ring (visible in model_hands.png). The fork's Frame controller meshes (memory: simplysanatori GitHub fork, Frame controller models) are not in this repo's assets; loading them needs nothing new (same .room format, painted from colours), only the asset.
- Unload while frames are in flight: mlxr_room_unload/rescale assume an idle GPU; the integration needs a 2-3 frame deferred destroy (or a vkDeviceWaitIdle on a style switch, ~ms).
- GPU time of the room pass is not measured here (the test times wall clock incl. readback: 31 ms for 2x1280x1280 with pipeline builds). M5 should log it with timestamp queries at the native eye size; shaders lane advises the per video frame cadence.
- ASTC with Turnip memory: image allocations come from the DEVICE_LOCAL type; on Turnip every type is host visible too, so a direct-upload path without staging is possible, but staging is 10 ms and freed immediately.
- `preferLowAtlas` and the _lo_ files could be dropped from the repo (7.5 MB) if the orchestrator agrees.
- Requests for other lanes: none required (no edits to xr/, shaders/, assets/ needed). `MLXR_ASSETS_DIR` is defined (cache var, same default) in rooms/CMakeLists.txt as well, because rooms is configured before xr/core.
