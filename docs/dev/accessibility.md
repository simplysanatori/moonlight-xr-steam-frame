# Accessibility: the cog tab, the eyes, and the gaze probe

Lanes a11y-tab and a11y-2 (2026-10-30). What the user sees is in [USER_GUIDE.md](../USER_GUIDE.md#accessibility). This page is
for whoever works on it next.

## The tab

The cog's fifth tab, `COG_TAB_ACCESS` (4; About moved to 5, `cog_tab` is kept by name so old files still open on the
right tab). Three rows, each the `[vr]` key it shows: Pointer (`pointer_source`: Controller, Head, Eyes), Dwell
click (`head_dwell_ms`: Off, 600, 900, 1200, 1500) and Dot size (`assist_dot_size`, a track of 50 to 300 % in 25 %
steps, a tick at each, the thumb's percent as its readout). Under them the eye tracking line and the instructions
(`UIS_ACCESS_HELP`, plus `UIS_ACCESS_HELP_EYES` while the eyes are not ready), and at the bottom a footer with one
sentence on the row under the pointer (`UIS_ACCESS_DESC_*`; `UIS_ACCESS_DESC_NONE` while it is on none).

Lane a11y-2 took Head-look, Back as Guide and Mouse mode off the tab: the owner's point was that "Head" on the
Pointer row already means "move the pointer with your head", so a second head row that turns a game's camera read as
the same thing, and the other two need hands (Back as Guide is a gamepad button) or are not about access (Mouse
mode). Their settings are unchanged; the launcher has them under Input Settings (head-look as "Head turns the game's
camera (first-person games)", its speed as "Head camera speed", Mouse mode) and Gamepad Settings (Back button as
Guide), where they were before lane a11y-tab. The tab bar's slots are no longer even: `COG_TAB_EDGE_*` in
`xr/core/xr_shared.h`, read by `cogTabAt`, `cogTabLeft`, `cogTabRight` (`xr/core/xr_layout.c`); the names shrink to
their slot when a translation is long.

| Where | What |
| --- | --- |
| `xr/core/xr_access.c` | Pure: the dwell row's times, the eye line's state (`accessEyesState`), whether the Eyes cell takes a press (`accessEyesCellLive`: always, unless the runtime does not offer eye gaze), the dot's scale (`accessDotScale`), the row at a point of the panel (`accessRowAt`, for the footer), the Eyes pointer's fallback to the head (`gazeFallStep`: the head after 200 ms lost, the eyes again after 300 ms tracked). `tests/xrcore/test_access.c` |
| `xr/xr_assist.c` | The tab on the XR side: the cells a row has, a press (applied to the context now, handed back as `SETTING_POINTER_SOURCE`, `SETTING_HEAD_DWELL`, `SETTING_ASSIST_DOT_SIZE`, 42 to 44, and stored by `xrStoreSetting`), the Dot size track (`cogAccessSliderValue`, `cogApplyAccessSlider`, `cogAccessDragEnded`, called from `xr/xr_ui.c`'s slider code), the footer's row (`xrAccessHoverRow` from `ctx->cogPointV`, which `xr/xr_input.c` sets wherever the pointer is on the cog, labels and greyed rows included), the dot's scale for `xr/xr_layers.c` (`xrAssistDotScale`), the eye line from what the session found, and `xrGazeWatch`, the once-a-session log line |
| `ui/panel_cog.c` `tab_access` | The drawing; `UiView.access_eyes` carries the eye line's state and `UiView.access_row` the footer's row (`xr/xr_panels.c` puts both in the cog's redraw key; the key's room went from 2 KB to 4 KB, since the whole `Settings` in it had grown past 2 KB and cut off everything after it) |
| `xr/xr_layers.c` | The pointer's dot and the dwell ring drawn at `pointerDotSize(distance) * xrAssistDotScale(ctx)`: `assist_dot_size` with the head or the eyes pointing, today's size with a controller |
| `xr/xr_input.c` | `headPointing` covers `POINTER_SOURCE_GAZE`: Eyes is head Point mode with the gaze's ray while the eyes are tracked (`readSources`), held through a blink. `dwellPanelHover`: with a menu open the dwell aims at the menu (before, the pass's hover named the picture behind it, so a dwell on a cog cell was a click on the hidden picture) |
| `xr/xr_session.c` | Lane a11y-2: the gaze extension is enabled, and the gaze action attached (`xr/xr_input.c`), whenever the runtime offers `XR_EXT_eye_gaze_interaction`, so the eye line can say whether gaze arrives and Eyes works without `config.ini`. Reading the gaze points nothing: the eyes point only with `gaze = true` (`gazeEnabled`) or `pointer_source = gaze` (`headPointing`). `supportsEyeGazeInteraction` is logged but plays no part: SteamVR's flag has read 0 on systems that gave gaze, so a pose arriving is the signal |
| `tests/m4/test_access.c` | Through the input pass's seams: presses (Eyes dimmed and dead with no eye gaze offered, live as soon as it is offered), the Dot size's steps and drag, nothing changing while the head and the eyes rest on every cell and step button for 3 s with the controllers pointing, the gaze read but pointing at nothing by default, the head alone turning Head off with a dwell on Controller, the eyes' dwell, a blink, the fallback to the head, the footer's row (labels, cells, greyed rows), and the dot and ring's sizes in the frame's layers |
| `tests/ui/test_panels.c` `test_access_tab` | The footer's sentence for each row (and the hint for none), the readout, Eyes dimmed and dead alone without eye gaze |

Never on by itself: nothing writes `pointer_source` or `head_dwell_ms` except a press on the tab, the launcher's two rows
and the file (`tests/ui/test_settings.c` `test_head_point_never_by_itself`, `tests/m4/test_access.c` `neverByItself`,
`tests/launcher/run_tests.sh` "no other row writes"). The first-run card has no buttons and writes only
`first_run_shown`. A press on the cell already in force changes nothing (before lane a11y-2 it started the dwell over,
so a dwell resting on a Dwell click cell clicked it again every dwell time).

The launcher's Accessibility rows (Pointer, Dwell click, Dot size, `launcher/qml/XrSettingsView.qml`) carry the
footer's sentences as tips (`PrefRow.tip`: a `ToolTip` while the mouse is over the row or the row has a keyboard's or
a gamepad's focus). `tests/launcher/run_tests.sh` gives each row the focus as a gamepad does and checks the tip is up
with its sentence (`showTip`, `MLXR_SETTINGS_TIP:`).

## Is eye gaze reaching the app? The gaze probe

**Result on a Steam Frame (2026-10-30):** exit status 0, "eye gaze reaches this app": a gaze pose on all 1440 frames of
the 20 s, its direction changing as the wearer looked around. SteamVR delivers eye gaze to OpenXR apps on the Frame;
the earlier "no gaze" was this app never asking for it (the extension was enabled only with `gaze = true` or
`pointer_source = gaze`, and the default config has neither). Since lane a11y-2 the app asks whenever it is offered.


`mlxr-hello --gaze-probe` answers it without the launcher or the client: it creates a session with
`XR_EXT_eye_gaze_interaction`, suggests `/interaction_profiles/ext/eye_gaze_interaction` with
`/user/eyes_ext/input/gaze_ext/pose`, attaches the action set, waits for focus, syncs every frame for 20 s and prints
the extension, `supportsEyeGazeInteraction`, the binding and attach results, the profile the runtime bound to
`/user/eyes_ext`, the action's `isActive` and the pose's location flags whenever they change, the gaze's direction
twice a second, and a summary. It changes nothing and exits by itself (a minute without focus ends it too).

On the Frame, from a terminal (SSH or the desktop's Konsole), with SteamVR up, the client closed and the headset
**worn** (focus needs it):

```sh
~/Games/MoonlightXRNative/src/build/hello/mlxr-hello --gaze-probe
```

(`mlxr-hello` is built by `build.sh` in the source tree, and `packaging/install.sh` from the source tree copies it to
`~/Games/MoonlightXRNative/app/`; release tarballs do not carry it.) `--seconds N` watches longer. During the watch,
look around, blink, and close both eyes for 1, 2 and 3 s: the lines then also answer the plan's question of whether
closed eyes drop the tracked bit. Exit status 0 means eye gaze reached the app, 3 means it did not, 1 an error.

Reading it: no `XR_EXT_eye_gaze_interaction` means the runtime does not offer it; `supportsEyeGazeInteraction 0` with
the action never active points at the headset's own setting (SteamVR Settings > General > Eye Tracking, community
report, see the user guide); active with no tracked pose means the eyes are not seen. Run it once, toggle the setting,
run it again.

## Later: eye gestures (not built)

The plan's closed-eyes click and pause (`assist_eye_gestures`, `assist_close_click_ms`, `assist_close_pause_ms`) wait
for the probe's answer on whether the gaze pose loses its tracked bit while the eyes are closed, and after how long
(the Frame does give the app eye gaze, see above; the closed-eyes lines of that run are still to be read). The Frame gives no per-eye openness, so closing both eyes is
the only gesture there can be. When it is built: feed `xrGazeValid()` (and `ctx->gazeFlags`) and the clock to a pure
timer in `xr/core` (nothing under 500 ms, a blink; the click at 1 s and more, the pause at 2.5 s), freeze the dot the
moment the gaze goes invalid, keep it off by default and behind its own switch, and keep the dwell working beside it.
The Action strip, Pause and Resume, snapping and the assist keyboard of the plan are not built either.
