# M4 report: the in-headset UI (lane/m4-ui)

Moonlight XR's renderer core (UI, input, layers, display rate, log) ported from
the Steam Frame fork of Gilleece's Moonlight XR onto a native Vulkan/OpenXR
session host, with lane U's panels drawn into one UI atlas, a launcher (PC list,
app list) in the headset, and config.ini behind every setting. Built on main
d7901e4, with main merged at 31b4b15 (quit on exit), de55376 (80 Mbps) and
a74444d (depth lane). Nothing here has been seen in the headset: SteamVR keeps a
session started over SSH in SYNCHRONIZED (shouldRender false), so what is
"headless-verified" below was proven with unit tests, the offscreen snapshot
tool and headless `--file` runs.

## Ported (upstream = fork branch steam-frame; logic kept, every change marked `native` in the code)

| File | Upstream lines | Native lines | What changed |
| --- | --- | --- | --- |
| `xr/xr_renderer.h` | 1841 | 872 | `XrCtx` without GL/EGL/JNI/depth staging/room GL; Vulkan device pointer and native glue added (atlas, panels, curve, launcher, `dotOn`, `ensurePassthrough`). Fork Frame fields kept (`padX..padStart`, `frameProfile`, `frameBound`, `onFrame[]`, `frameInteraction`). Android log priorities defined locally. |
| `xr/xr_input.c` | 3922 | 3949 | Unchanged logic (actions, the fork's `suggestFrameBindings`, hands, gaze, ray, grabs, every modal, keyboard, pad, head aim, rumble). `nativeUpdateInput` → `xrUpdateInput(... float* out)` filling the same `IN_` slots; `nativeSetRumble/ScreenPose/HandHint` → C. New: `updateLauncher` (the launcher list takes presses on the picture), `xrHoverAt` (tests). |
| `xr/xr_ui.c` | 1326 | 1336 | Unchanged logic. JNI setters → `xrSetDepthDefaults/ClickSound/ShowRay/HeadAim/Gamepad/Picture`. Native: `cogRowLive` also asks `xrRowLater`; the ray row's third cell (dot); `setPadMode` refuses gamepad mode until M3 (toast); `effectiveCurvature` is 0 while the launcher's flat list has the picture's place (`xrCurveSetting` is the stored curve). |
| `xr/xr_layers.c` | 1679 | 865 | Upstream's order, poses, sizes, fades, drop order, the fork's transparent passthrough projection layer. Every piece of art is a sub-rect of the UI atlas swapchain. The cog panel's rings/thumbs/marks/readouts are drawn into the panel by lane U (one quad + clock strip); picker, keyboard, hint and Ko-fi rings stay upstream's outline quads. Room, glow, model and splash layers not built (later milestones). `nativeEndFrame` → `xrBuildLayers` (the host draws the video and calls `xrEndFrame`). New: launcher list layer; `xrCurveShape/xrCurveMvp`. |
| `xr/xr_display.c` | 685 | 639 | Unchanged (refresh rate requests and budget, perf levels). JNI getters → `xrSetDepthRate`, `xrGetDisplayRate`; the depth gate getters dropped until M6. |
| `xr/xr_log.c` | 271 | 331 | Same writer thread and queue. Native: writes `~/Games/MoonlightXRNative/moonlight.log` itself, rolled to `moonlight.log.1` past 5 MB (two files); stdout stands in for logcat only on a terminal (`MLXR_LOG_STDOUT=1` forces it); `mlxr_logf` (stream/app side) lands here at event level; `xrLogFlush`. |
| `xr/xr_assets.c` | 1024 | 425 | Kept: `buildHandleArt` and `uploadPointerArt` drawing (pill, corner, outline ring, beam, dot; rows top down, no GL flip), `roomStyleForCell`, `roomCellForStyle`, `nativeSetEnvironment` → `xrSetEnvironment`. New: the UI atlas swapchain (below). |
| `xr/xr_session.c` | 1293 | 1559 | Rewritten as the host (see below); `pollEvents`, `handleSessionStateChange`, `runtimeEnded`, the instance's extension probing follow upstream. `checkXr`/`xrResultName` moved to `xr/xr_result.c`. |
| `stream/vr_keyboard.c` | VrKeyboard.java 133 | 126 | Same rules (VK codes, shift around capitals, held Ctrl/Alt/Win, punctuation as keys with a modifier, the rest as UTF-8 text). |

## New

- `xr/xr_assets.c` atlas: one `R8G8B8A8_SRGB` swapchain 2048x2960 (lane U's layout) holding every panel, the 12 bar button faces, and six code-drawn slots in free corners (beam, dot, ring, corner, pill, stats). The CPU copy is a host-visible staging buffer; a slot redrawn bumps its version and is copied into each swapchain image as that image comes round (only changed slots, only when something is stale). sRGB as upstream's `GL_SRGB8_ALPHA8`; premultiplied.
- `xr/xr_panels.c`: lane U's rasterisers into the atlas when a panel's inputs change (a key per panel). Measured drawing costs (snapshot tool, -O2): cog 3.9-4.9 ms, exit 1.3 ms, stats 1.3 ms, keyboard 9-11 ms (sheet or modifier change only; the key under the ray is the ring quad), picker 12.4 ms (on open), app/PC list ~9 ms (on hover change). Drawing is on the frame loop; a worker thread is the next step if the list hover shows a dropped frame.
- `xr/xr_prefs.c`: config.ini ↔ `XrCtx` (what nativeInit and the setters carried, and XrRenderer.applySetting/saveScreenPose), the [screen] keys ↔ the 10-float pose, which rows come later.
- `xr/xr_later.c`: rooms, glow and models answer "off" (`roomEffective` 0 etc.).
- `xr/xr_session.c` host: one instance/session for the app's life; frame loop = poll events (states, `REFERENCE_SPACE_CHANGE_PENDING` → `recentreScreen` + head aim hold, interaction profile, refresh rate) → `xrUpdateInput` → dispatch (host mouse/buttons/scroll/head-aim relative mouse, keyboard through VrKeyboard, exit, settings and pose saved to config.ini, environment, toasts, report) → the app's launcher hook → panels into the atlas → one command buffer (atlas copies, the M2 hardware frame or a software frame warped through `mlxr_pass_warp` with the Picture grade and the Display tab's sharpening, the curved picture) → `xrBuildLayers` → `xrEndFrame`. A still picture is drawn again when the grade, sharpening or curve changes (the newest hardware frame is held for it and given back once a newer one is drawn). The software path now uses the same warp pass (M1's own shader removed).
- Curve: no cylinder layers on SteamVR, so when curved the warp goes into an app-owned image and every display frame `mlxr_pass_panel` draws it on upstream's cylinder into a two-eye projection layer (recommended eye size). The bar, corners and ray hits use the same surface (tested).
- `app/launcher.c` + `app/main.c`: the launcher. PC list from hosts.ini, online/paired/running from serverinfo on a worker thread (refreshed every 10 s while the list is up); a PC opens its app list (applist); an app launches or resumes (another running app is ended first, as Moonlight asks); not paired → toast (pair with mlxr-cli; pairing from the headset is later, so is Add PC); offline with a MAC → Wake-on-LAN. Exit from a stream: stream stopped, host session ended (`mlxr_server_quit_app`), back to that PC's app list. Exit from the lists ("Quit Moonlight XR?") or the app being closed by SteamVR: stream stopped and host session ended first. `--host/--app` autostart (exit returns to that PC's app list); `--file` plays the clip and exit quits. Upstream's prompt has only Exit/Cancel, so Exit is the quit.
- Settings: `[vr] pointer_dot` (new, owner's request); default bitrate 80 Mbps for 1440p60-90 (owner, main de55376); `gaze` and `hand_tracking` default off natively (see Deviations).

## Swapchain budget (SteamVR: 16 a session)

| Swapchain | When |
| --- | --- |
| the picture (video) | from the first decoded frame |
| the UI atlas | always |
| passthrough's 16x16 transparent projection layer (fork trick) | first time passthrough is chosen |
| the curved picture's projection layer | first time the curve is above 0 |

At most 4; logged as "swapchains alive N of 16" when each is made and in the 5 s stats line. Layers: the busiest frames measured by `m4_budget`/the snapshot tool are 12 (+1 passthrough): picture, stats, pill, six bar buttons, ray, dot, toast.

## Bar buttons

| Button | State |
| --- | --- |
| Exit (+ prompt) | works, headless-verified (hit test, prompt art, launcher state machine: stop + host quit + app list; from the lists it quits); needs headset check |
| Environment (picker) | passthrough and void work (headless-verified hits/art; passthrough needs headset check); the three rooms are dimmed "Later" (M5) |
| Move (pill, 6DOF drag) | ported unchanged; needs headset check |
| Cog (settings) | works, headless-verified; needs headset check |
| Keyboard | works: keys through VrKeyboard → LiSendKeyboardEvent/LiSendUtf8TextEvent, Fn sheet, held Ctrl/Alt/Win (unit-tested); needs headset check |
| Ray | works (session toggle as upstream); needs headset check |
| Head aim | shown only head locked (as upstream); relative mouse via `LiSendMouseMoveEvent`; needs headset check |
| Gamepad | hidden until M3 (the controllers' shortcut is refused with a toast; the owner's menu/View + grip toggle comes with M3) |
| 3D | hidden until M6, as upstream hides it without stereo; the 3D tab says why |
| Corner resize handles | ported unchanged; needs headset check |

## Settings rows

| Tab / row | State |
| --- | --- |
| Screen: Distance, Height, Tilt, Rotate, Size | work live, saved to `[screen]` on release; thumbs drawn from config.ini equal xr_ui.c's slider values (tested); needs headset check |
| Screen: Curve | works through the panel pass (geometry tested); needs headset check (GPU path not reachable headless) |
| Screen: Aim sensitivity, dead zone | live when head locked (as upstream), saved; needs headset check |
| Screen: Reset | back to config.ini's default placement; needs headset check |
| Display: Sharpen | works (warp pass sharpen 0.4 / 0.8; upstream default Quality, so it is ON by default: compare Off in the headset) |
| Display: Supersample | greyed "comes in a later update" (no layer settings on SteamVR; an app-side pass later) |
| Display: Stats | works (overlay strip in the atlas); needs headset check |
| Display: Head locked | works (view-space picture and furniture), saved |
| Display: Head aim | live when head locked; session toggle |
| Display: Controllers (gamepad) | greyed until M3 |
| Display: Pointer sleep | works (upstream's wake/sleep gate), saved |
| Display: Ray: Off / Dot / Ray | works: Ray = ray and dot, Dot = dot only (upstream's "off"), Off = neither (only the host's cursor, which Sunshine draws into the video and the protocol cannot hide); the dot is saved (`pointer_dot`), the ray is per session as upstream |
| Display: Controller model, Click sound, Glow, Screen light, Glow level | greyed "comes in a later update" (models, the click, the glow; screen light with rooms in M5) |
| 3D tab (preset, depth, convergence, switch, reset) | greyed until M6: "3D comes with the depth model in a later update" |
| Picture: Brightness, Contrast, Gamma, Saturation, Reset | work live through `mlxr_pass_warp` gradeOn/grade, saved; a still picture redraws; needs headset check |
| About: Report a problem | sheet + keyboard; Send writes the note to the log and `~/Games/MoonlightXRNative/reports/`; Ko-fi sheet shows |
| Room tab | never shown (no room up until M5) |

## Headless evidence

- ctest: 73 of 73 pass with `-E audio_pulse` (the 67 of main incl. depth's 7, minus audio_pulse, plus `m4_snapshot_budget`, `m4_hits` 1134 checks, `m4_prefs` 1602, `m4_launcher` 34, `m4_vrkb` 12, `m4_budget` 194). `tools/ldd-audit.sh build-m4/moonlight-xr`: ok.
- Snapshots: `ninja -C build ui-snapshots` writes `build/ui-snapshots/*.png` (18 states: picture, bar, cog hover, each tab, keyboard, Fn sheet, exit prompt (stream and launcher), picker, stats, toast, corner, PC list, app list). Looked at: bar, Display tab, Screen tab, keyboard, picker, PC list, app list + exit.
- `--file test-1080p60.mp4 --seconds 12`, hardware decode, software decode and with `curve = 50`: clean exits (rc 0), no E/W lines, the curve's own image and projection swapchain made (3 swapchains), hw decode 1.5 ms, 0 errors. Kernel log over the runs: no iris/msm_gem/smmu/hangcheck lines (only the camera ISP's usual CSID lines).
- Launcher against the backend's fake Sunshine host (paired with mlxr-cli in a scratch config): `--host fake-host --app Desktop` found the host, read 2 apps, launched Desktop and started the stream (RTSP refused by the fake host, as expected); the PC list run read the host as busy.

## Deviations and limits

- `gaze` and `hand_tracking` default to off natively: with gaze on, upstream gives the pointer to the eyes and a controller takes it back only on a press, which changes the feel the owner accepted in M2. One line in config.ini turns either on (code ported and wired).
- Pointer smoothing keeps M2's values (One Euro 1 Hz, 0.01 per pixel/s) instead of upstream's (1.2 Hz, beta 8 per picture width), and the aim pose filter is passed through (upstream filters it too); upstream's wake/sleep gate is in (Pointer sleep row turns it off).
- `[screen]` holds the placement in cm/degrees: a screen moved more than 2 m above or below eye level is saved at the 2 m limit; the yaw is saved as facing the seat.
- Panel drawing is on the frame loop (costs above).
- Display rate: requests follow upstream; SteamVR picks its own rate (a refused request is one W line).
- Splash not drawn (starts gone). Click sound, controller model, glow, rooms, gamepad mode, 3D: later milestones.

## Headset test script (owner)

1. Launch "Moonlight XR Native" from Steam with no arguments. Expect the PC list ("Computers") where the screen would be; gaming-pc shows Online, Paired.
2. Point at gaming-pc and pull the trigger: the app list appears. Page down/up with the arrows; Back returns to the PCs; open gaming-pc again.
3. Pick Desktop: a toast "Starting Desktop", then the desktop. Check the pointer, trigger click, double click, drag, A / d-pad-down right click, stick scroll, and the Bluetooth keyboard.
4. Move the ray just under the picture: the bar appears (exit, environment, pill, cog, keyboard, ray). Grip or trigger the pill and drag: the screen follows the hand and stays square to you. Hover a corner and drag: it resizes about its centre.
5. Cog: Screen tab: step Distance and Size with the chevrons, drag Tilt and Rotate (they snap level in the middle), raise Curve: the picture bends, the bar follows it, clicks still land where the dot is. Reset puts it back.
6. Display tab: Sharpen Off / Normal / Quality (compare); Stats On (strip top left); Head locked On (picture follows the head; Head aim row and bar button appear; try head aim on and off); Ray: Off / Dot / Ray. Greyed rows say "comes in a later update".
7. Picture tab: drag Contrast and Saturation on a still desktop: the picture changes at once; Reset.
8. Keyboard button: type into a text field on the PC; ?123 and Fn sheets; Ctrl lit + c copies; Hide closes it.
9. Environment button: Passthrough (the room appears around the picture), Black void back. Room tiles say "Later".
10. Hold the Steam/system recentre: the picture comes back in front at the same distance and size.
11. Exit button → "Exit the stream?" → Exit: the app list comes back and the desktop session has ended on the PC (Sunshine shows nothing running). Exit again from the lists → "Quit Moonlight XR?" → Exit: the app quits.
12. Close the app from SteamVR's menu while streaming: the PC session ends too.
13. Send `~/Games/MoonlightXRNative/moonlight.log` (and `moonlight-console.log`) for review.
