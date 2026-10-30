# Accessibility: the cog tab, the eyes, and the gaze probe

Lane a11y-tab (2026-10-30). What the user sees is in [USER_GUIDE.md](../USER_GUIDE.md#accessibility). This page is
for whoever works on it next.

## The tab

The cog's fifth tab, `COG_TAB_ACCESS` (4; About moved to 5, `cog_tab` is kept by name so old files still open on the
right tab). Five rows of cells, each the `[vr]` key it shows: Pointer (`pointer_source`), Dwell click
(`head_dwell_ms`: Off, 600, 900, 1200, 1500), Head-look (`head_look`), Back as Guide (`back_as_guide`), Mouse mode
(`mouse_mode`). Under them the eye tracking line and the instructions (`UIS_ACCESS_HELP`, plus
`UIS_ACCESS_HELP_EYES` while the eyes are not ready). The tab bar's slots are no longer even: `COG_TAB_EDGE_*` in
`xr/core/xr_shared.h`, read by `cogTabAt`, `cogTabLeft`, `cogTabRight` (`xr/core/xr_layout.c`); the names shrink to
their slot when a translation is long.

| Where | What |
| --- | --- |
| `xr/core/xr_access.c` | Pure: the dwell row's times, the eye line's state (`accessEyesState`), the pointer row's cells, the Eyes pointer's fallback to the head (`gazeFallStep`: the head after 200 ms lost, the eyes again after 300 ms tracked). `tests/xrcore/test_access.c` |
| `xr/xr_assist.c` | The tab on the XR side: the cells a row shows, a press (applied to the context now, handed back as `SETTING_POINTER_SOURCE` ... `SETTING_MOUSE_MODE`, 42 to 46, and stored by `xrStoreSetting`), the eye line from what the session found, and `xrGazeWatch`, the once-a-session log line |
| `ui/panel_cog.c` `tab_access` | The drawing; `UiView.access_eyes` carries the eye line's state (`xr/xr_panels.c` puts it in the cog's redraw key) |
| `xr/xr_input.c` | `headPointing` covers `POINTER_SOURCE_GAZE`: Eyes is head Point mode with the gaze's ray while the eyes are tracked (`readSources`), held through a blink. `dwellPanelHover`: with a menu open the dwell aims at the menu (before, the pass's hover named the picture behind it, so a dwell on a cog cell was a click on the hidden picture) |
| `xr/xr_session.c` | The gaze extension is asked for with `gaze = true` or `pointer_source = gaze`. `supportsEyeGazeInteraction` is logged but no longer drops the gaze action: SteamVR's flag has read 0 on systems that gave gaze, so a pose arriving is the signal |
| `tests/m4/test_access.c` | Through the input pass's seams: presses, nothing changing while the head and the eyes rest on every cell for 3 s with the controllers pointing, the head alone turning Head off with a dwell on Controller, the eyes' dwell, a blink, the fallback to the head |

Never on by itself: nothing writes `pointer_source` or `head_dwell_ms` except a press on the tab, the launcher's two rows
and the file (`tests/ui/test_settings.c` `test_head_point_never_by_itself`, `tests/m4/test_access.c` `neverByItself`,
`tests/launcher/run_tests.sh` "no other row writes"). The first-run card has no buttons and writes only
`first_run_shown`.

## Is eye gaze reaching the app? The gaze probe

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
for the Frame to give the app eye gaze at all, and then for the probe's answer on whether the gaze pose loses its
tracked bit while the eyes are closed, and after how long. The Frame gives no per-eye openness, so closing both eyes is
the only gesture there can be. When it is built: feed `xrGazeValid()` (and `ctx->gazeFlags`) and the clock to a pure
timer in `xr/core` (nothing under 500 ms, a blink; the click at 1 s and more, the pause at 2.5 s), freeze the dot the
moment the gaze goes invalid, keep it off by default and behind its own switch, and keep the dwell working beside it.
The Action strip, Pause and Resume, snapping and the assist keyboard of the plan are not built either.
