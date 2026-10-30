# Placement: presets, bring here, Follow, angular lock, per app

PLAN Wave 3 rows 3.1, 3.2, 3.3 and 3.12, and wave 4b row 4b.6 (zoom and pan, at the end). Where the code is and how it fits.

## Pure maths: `xr/core/xr_layout.{h,c}`

- `layoutPresetOf`, `layoutPresetAt`: Desk 120 cm away / 160 cm wide, Couch 250 / 260, Cinema 400 / 500. The ring on the Screen tab's Preset row comes from `layoutPresetAt` (a couple of centimetres either way counts).
- `layoutGazeDir`, `layoutFaceHead`, `layoutScreenAlong`, `layoutBringHere`: the screen on a direction from the head, the distance kept, facing the head with its tilt and roll kept. The gaze is held to 40 degrees up or down.
- `FollowState`, `followOutsideLeash`, `followStep`: Follow. The leash is checked on yaw and on pitch separately. Past it the screen eases (a smoothstep slerp, `FOLLOW_EASE_SEC` = 0.6 s) from where it is to where the head looks; a gaze that moves more than half a leash from the target while it eases restarts the ease from the screen's current direction. Landed, it is world locked again and the pose is marked dirty so it is saved like a moved screen.
- `layoutAngularWidth`: the width that keeps the same angle after a push or pull.

## Glue: `xr/xr_place.c`

`xrLayoutFrame(ctx, settings, now)` runs once a frame at the end of `dispatchInput` in `xr_session.c` (after the input pass, before the layers are built). It locates the head in the local space, applies the stream identity, runs the angular lock (`grabMode == GRAB_MOVE` and `grabPush != 0`: the width is the width before the push began times distance over distance-without-push, and the radius scales with it), runs Follow (not under a panel, not while the screen is held, not head locked, not in a room, not in the launcher) and keeps the per-app layout. It returns 1 when the settings changed and want saving.

`xrBringScreenHere(ctx)` is what the Screen tab's Bring here cell and the `app:bringhere` action call. Returns 1 if it moved the screen. **For the binding lane:** in `xr_input.c`, `XR_BIND_APP_BRINGHERE` (the `bringhere` code in `xr_bind.c`) should call `xrBringScreenHere(ctx)`; the function is declared in `xr_native.h`. It needs no arguments, uses the head pose `xrLayoutFrame` located this frame, and does nothing (returns 0) in a room, with Head locked, or before the first frame has a head pose.

## Per PC and app

- `app/main.c` `launchStream` calls `xrSetStreamIdentity(pcName, appName)` once the host has started the app. A hash of the two names (lower case, FNV-1a) keys the layout; no app name, no layout.
- `[screen.<hash>]` sections in `config.ini` hold `AppLayout`: the `[screen]` keys, `head_locked`, `head_follow`, `stereo`, `separation`, `convergence`. At most `APP_LAYOUT_MAX` (32) are kept, the oldest dropped. `ui/settings.c` reads and writes them (`settings_layout_*`).
- A new identity first writes what was pending for the old app, then puts the new app's layout in force (settings, then the context the way a session start does, the 3D state through `setStereoLive`). An app with no layout keeps what is in force.
- The layout in force is compared with the last one seen each frame; a change must sit for 750 ms before it is stored (a drag is one write). Forget (`SETTING_FORGET_LAYOUT`) drops the entry and does not store again until something changes. `per_app = false` stops both storing and putting back.

## Settings and rows

- `head_locked` stays 0/1 for every consumer; `head_follow` is the third cell (`SETTING_HEAD_LOCK` carries the cell 0, 1 or 2). `settings_clamp` drops `head_follow` when `head_locked` is on.
- Screen tab rows 8..10 are cells: Preset (Desk, Couch, Cinema, Bring here), Remember per app (Off, On, Forget) and Angular-size lock (Off, On). New setting ids: `SETTING_PER_APP`, `SETTING_FORGET_LAYOUT`, `SETTING_ANGULAR_LOCK` (`xr_native.h`).

## Headset checks (not done in CI)

1. Follow: leash 25 degrees, look around inside it, nothing moves; turn past it, the screen glides after the gaze and stops; sit for ten minutes with it on for nausea. Try 40 and 60 degree leashes.
2. Presets: the three look right at a desk, on a couch, in a room.
3. Bring here: look to one side, press the cell (or the bound button), the screen arrives in front of you at the same distance and size.
4. Angular lock: grip the pill and push a thumbstick north, the picture keeps its size in view; with the lock off it shrinks.
5. Per app: lay out two apps differently, switch between them, quit and relaunch.

## Zoom and pan (PLAN 4b.6)

A part of the picture fills the screen. One rectangle does it for the flat picture, the curved picture, both 3D eyes, the pointer and the glow.

- **State.** `[display] zoom` (100..400 %), `zoom_x`, `zoom_y` (the centre, 0..100 % of the picture across and down). In the context `zoomPct`, `zoomX`, `zoomY` (set by `xrApplySettings`, written back by `xrSettingsFromCtx`), and `zoomHitU/V/Valid`, where the pointer last landed on the screen (`dispatchInput` fills them from `IN_U`/`IN_V`).
- **Maths, `xr/core/xr_layout.{h,c}`.** `zoomRect(pct, cx, cy, rect)` is the part in the picture's own uv (`{x0, y0, w, h}`, always the picture's shape, w = h = 100 / pct, slid inside the picture when the centre is near an edge). `zoomStepPct` is the ladder 100, 125, 150, 175, 200, 250, 300, 350, 400. `zoomPointerUv` maps a screen uv through the rect and `zoomScreenUv` back. `zoomAnchorCentre` is the aimed zoom: the centre that keeps the picture point under the pointer under it (a part that would touch an edge is stored as that edge's centre, 0 or 100, which pins it there exactly). `videoShownRect(source, zoom, out)` is what the screen shows of the decoded frame: the picture is the whole frame, or with a host-rendered pair (SBS or TB) the left or top half, and the zoom rect is inside it. `videoGlowRect` narrows that to the content inside the letterbox bars.
- **Glue, `xr/xr_zoom.c`.** `xrZoomStep(ctx, +1 | -1 | 0)` is the binding action's whole body (zoom in, out, reset). It moves `zoomPct/X/Y` and returns 1 if they moved. `xrZoomSync` (called from `xrLayoutFrame` after the identity step) copies a changed context zoom into the settings, so it is saved (`config.ini` is written when `xrLayoutFrame` returns 1) and the layout compare below sees it. `xrZoomRect`/`xrZoomPointer` give the rect and the mapping for a context (not zoomed when `zoomPct` was never set).
- **Drawing, `shaders/warp.frag`.** The warp's `v_screen` (the pixel's place on the target) becomes `v_plain = zoom.xy + v_screen * zoom.z` first thing in `main`, and everything below works on that: the depth maps (they are in frame space), the shift, the edge fade, and the SBS/TB half (`halfTc` spreads the zoomed picture uv over the eye's half, so the zoom is inside the half and the seam hold is unchanged). The push block was full at 128 bytes, so the four switches (occlusion, cubic depth, show depth, bar test) share `modes.x` as bits and `modes.yzw` carry `zoomX, zoomY, zoomSize` (`MlxrWarpParams`; size 0 or 1 is the whole picture). `xrDepthRecordWarpEyes` fills them from the context for every eye, so the curved picture and the 3D eyes get the zoom without a second mechanism. `videoStale` (xr_session.c) redraws a still picture when the zoom moved (`Video.drawnZoom`).
- **Pointer, `xrDepthPointerUv`.** The zoom first, then, on a host-rendered pair, `sbsPointerUv`. Callers need nothing: `dispatchInput` already calls it.
- **Glow, `xr_ambilight.c`.** `glowSampleRect` samples what is shown (`videoShownRect` of the zoom, inside the left or top half with a pair) narrowed by the detected bars (`videoGlowRect`). The bar detector itself still looks at the whole frame (the bars are the frame's, not the view's). A zoom that moved makes `glowStale` true and lands the next sample whole instead of easing from the old colours (`ctx->glowZoom`).
- **Per app.** `AppLayout` gained `zoom`, `zoom_x`, `zoom_y` (`LAYOUT_KEYS` in `ui/settings.c`, `[screen.<hash>]` keys of the same names); `layoutIntoContext` puts a kept one in force. `layoutSeen` is 16 ints now. An app with no layout keeps the zoom in force, as it does the placement.
- **Bindings.** `app:zoomin`, `app:zoomout`, `app:zoomreset` are `XR_BIND_APP_ZOOMIN/ZOOMOUT/ZOOMRESET` (8, 9, 10). **For the owner of `xr_input.c`:** in `bindAppCommand` add

  ```c
  case XR_BIND_APP_ZOOMIN: xrZoomStep(ctx, 1); break;
  case XR_BIND_APP_ZOOMOUT: xrZoomStep(ctx, -1); break;
  case XR_BIND_APP_ZOOMRESET: xrZoomStep(ctx, 0); break;
  ```

  `xrZoomStep` is declared in `xr_native.h`. The binding table, the parser and the launcher's Controller bindings page already know the three names; until the cases are in, a bound zoom button does nothing.
- **Not touched, on purpose.** The click hold, press fix and double-click radii in `xr_input.c` are in host pixels worked out from the screen uv times the video size, so at 400 % they are four times as wide in the picture as they look. The ray, the dot and the beam stay in screen uv, which is right. Retune those radii by the zoom there if it matters.

Tests: `tests/xrcore/test_layout.c` (`testTheZoom`: rect, clamping, ladder, pointer round trip, anchor, zoom inside the SBS and TB halves, glow rect), `tests/shaders` `warp_zoom` (a ramp read at the right sub-rect flat, inside either eye's half, and with a disparity the zoomed picture is the crop of the unzoomed one), `tests/m4/test_place.c` (`zoomMemory`: stored per app, put back, written and read, limits), `tests/m4/test_bindings.c` (`zoomActions`), `tests/m6` (the pointer inside the half), `tests/xrcore/test_bind.c`, `tests/launcher/run_tests.sh` (the Zoom row).

