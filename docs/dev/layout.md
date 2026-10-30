# Placement: presets, bring here, Follow, angular lock, per app

PLAN Wave 3 rows 3.1, 3.2, 3.3 and 3.12. Where the code is and how it fits.

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
