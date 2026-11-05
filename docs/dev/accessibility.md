# Accessibility: the cog tab, the eyes, and the gaze probe

Lanes a11y-tab, a11y-2 and radial-dwell (2026-10-30), a11y-final (2026-10-31). What the user sees is in [USER_GUIDE.md](../USER_GUIDE.md#accessibility). This page is
for whoever works on it next.

## The tab

The cog's fifth tab, `COG_TAB_ACCESS` (4; About moved to 5, `cog_tab` is kept by name so old files still open on the
right tab). Four rows, each the `[vr]` key it shows: Pointer (`pointer_source`: Controller, Head, Eyes), Dwell
click (`head_dwell_ms`: Off, 600, 900, 1200, 1500), Dwell click style (`assist_dwell_style`: Simple, Radial menu;
lane radial-dwell, below) and Dot size (`assist_dot_size`, a track of 50 to 300 % in 25 %
steps, a tick at each, the thumb's percent as its readout). Under them the eye tracking line and the instructions
(`UIS_ACCESS_HELP`, plus `UIS_ACCESS_HELP_EYES` while the eyes are not ready), and at the bottom a footer with one
sentence on the row under the pointer (`UIS_ACCESS_DESC_*`; `UIS_ACCESS_DESC_NONE` while it is on none), up to three
lines.

Lane a11y-final: the Headset button clicks row is gone (the feature is for later, see "Future: the headset button"
below), and the eye line, the instructions and the footer moved up into its room (`COG_ACCESS_STATUS_V`,
`COG_ACCESS_HELP_T`, `COG_ACCESS_FOOTER_*` in `xr/core/xr_shared.h`). The Eyes cell carries a small "experimental"
under its name (`tagged_cell` in `ui/panel_cog.c`, `UIS_CELL_EYES_EXPERIMENTAL`; one line did not fit a third of the
track at a readable size, so the two are stacked; a translation too long for the cell drops the tag and the
footer's sentence still says it). The pointer row's sentence says Eyes is experimental and jittery and how to get the
controllers back, and so do the instructions. The launcher's Pointer row names the cell "Eyes (experimental)", and
its tip is the same sentence.

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
| `xr/core/xr_access.c` | Pure. Lane a11y-final: `pointerBackStep`, the right hand's Menu and grip (below); `gazeSteadyStep`, the Eyes pointer's steadying (below); `accessDwellToleranceDeg`, the dwell's wander, 1 degree for the head, 2 for the eyes (`DWELL_TOLERANCE_EYES_DEG`). From lanes a11y-tab and a11y-2: the dwell row's times, the eye line's state (`accessEyesState`), whether the Eyes cell takes a press (`accessEyesCellLive`: always, unless the runtime does not offer eye gaze), the dot's scale (`accessDotScale`), the row at a point of the panel (`accessRowAt`, for the footer), the Eyes pointer's fallback to the head (`gazeFallStep`: the head after 200 ms lost, the eyes again after 300 ms tracked). `tests/xrcore/test_access.c` |
| `xr/xr_assist.c` | Lane a11y-final: `xrPointerBackStep` (from pointer mode's bindings frame, `bindPointerStep` in `xr/xr_input.c`, reset with everything else the bindings let go) and `xrEyesSteady` (from `readSources`, in place of the head's One Euro on the gaze's rotation while the eyes point). The tab on the XR side: the cells a row has, a press (applied to the context now, handed back as `SETTING_POINTER_SOURCE`, `SETTING_HEAD_DWELL`, `SETTING_ASSIST_DOT_SIZE`, 42 to 44, and stored by `xrStoreSetting`), the Dot size track (`cogAccessSliderValue`, `cogApplyAccessSlider`, `cogAccessDragEnded`, called from `xr/xr_ui.c`'s slider code), the footer's row (`xrAccessHoverRow` from `ctx->cogPointV`, which `xr/xr_input.c` sets wherever the pointer is on the cog, labels and greyed rows included), the dot's scale for `xr/xr_layers.c` (`xrAssistDotScale`), the eye line from what the session found, and `xrGazeWatch`, the once-a-session log line |
| `ui/panel_cog.c` `tab_access` | The drawing; `UiView.access_eyes` carries the eye line's state and `UiView.access_row` the footer's row (`xr/xr_panels.c` puts both in the cog's redraw key; the key's room went from 2 KB to 4 KB, since the whole `Settings` in it had grown past 2 KB and cut off everything after it) |
| `xr/xr_layers.c` | The pointer's dot and the dwell's circle drawn at `pointerDotSize(distance) * xrAssistDotScale(ctx)`: `assist_dot_size` with the head or the eyes pointing, today's size with a controller |
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

## The way back to the controllers: right Menu and right grip (lane a11y-final)

The owner's ask: "There needs to be a way to revert control back to the controller. It should be the right hand: the
Menu button plus grip, similar to the left hand's View plus grip." With Pointer on Head or Eyes, the right Menu button
and the right grip held together for `POINTER_BACK_HOLD_NS` (500 ms, the left hand's `PAD_TOGGLE_HOLD_NS`) set
`pointer_source` to controller, as a press on the tab's Controller does (`xrSetPointerSource`, the dwell time kept),
hand `SETTING_POINTER_SOURCE` back to be stored on the first frame the setting slot is free, and say "Controllers
point again" (`UIS_TOAST_POINTER_BACK`). Done the same way as the gamepad switch's left View + left grip
(`padToggleStep`): a pure step over the two buttons and the clock (`pointerBackStep`, `xr/core/xr_access.c`), fed from
pointer mode's bindings frame with the controls the bindings see (`xrPointerBackStep`, before the grips are masked for
the UI's grab).

Menu is the cog's (`app:cog`, or whatever `bindings.ini` gives `right.menu`), so the step keeps it from the bindings
while it may still be the chord's: let go before 500 ms with the grip never held, it reaches them as a one frame press
on the release (the cog opens as Menu is let go, a frame later than before); held alone for 500 ms it goes through as
held from then on (a binding that holds still works, and the chord can no longer form in that press); with the grip
held at any time during the press, Menu does nothing but the chord. The grip is not held back (in pointer mode it only
grabs the UI's handles, and with the head pointing no controller points at them). After a reset (a mode switch, focus
lost, the pointer off: `bindLetGo`) a Menu already down is no press until let go. It works whatever the pointer is;
with Controller it fires nothing (a log line) and still keeps the cog shut. Pointer mode only: in gamepad mode Menu is
Start and the grip RB, which a game may want together. `bindings.ini` refuses a pointer-mode chord with both
(`reservedWhy` in `xr/core/xr_bind.c`; grips were already refused there), like the mode switch.

Tests: `tests/xrcore/test_access.c` `testPointerBack` (the timing, a tap, the grip first or late, given up, held alone,
after a reset), `tests/xrcore/test_bind.c` (refused), `tests/m4/test_access.c` `pointerBack` (through the bindings
frame with the pointer pass: Head and Eyes to Controller, saved and said, a Menu tap opening the cog, the chord never
opening it, the setting slot taken that frame, gamepad mode), `tests/m4/test_bindings.c` (Menu opens the cog on the
release).

## Eyes: experimental, and steadied (lane a11y-final)

The owner, after trying it: Eyes is "experimental and really not great: the eye jitters so much and it's not accurate
at all". It is labelled experimental (above), and steadied cheaply. The head pointer's One Euro filter (the controllers'
settings) let the dot wander about 2 degrees on a gaze jittering by a degree round one spot (`tests/m4/test_access.c`
`eyesSteady`). `gazeSteadyStep` (`xr/core/xr_access.c`) replaces it on the gaze's direction while the eyes point
(`xrEyesSteady`, from `readSources`; the position keeps the head's filter, the fallback to the head keeps the head's
filters, and a switch between the two resets both):

- the direction is smoothed with a time constant of 120 ms (`GAZE_STEADY_SMOOTH_S`);
- the dot holds still while the smoothed gaze stays within 1 degree of it (`GAZE_STEADY_HOLD_DEG`: a fixation); past
  that it catches up with a 40 ms time constant (`GAZE_STEADY_FOLLOW_S`) until it is within 0.2 degrees
  (`GAZE_STEADY_SETTLE_DEG`), and holds again;
- a gaze 4 degrees from the smoothed one (`GAZE_STEADY_JUMP_DEG`) on two frames running is a saccade: the smoothing
  jumps there (the mean of the two), so a real look elsewhere is not dragged out; one wild frame is ignored.

Measured (`tests/xrcore/test_access.c` `testGazeSteady`, 90 Hz): +-1 degree of noise in yaw and pitch round a
fixation moves the settled dot by 0 degrees over 4.5 s (the check is under 0.3) and leaves it 0.1 degrees from the
fixation; a 10 degree look is reached within half a degree in 12 frames (130 ms); a slow look across at 5 degrees a
second lags by up to about 1.6 degrees (it holds, then catches up: a staircase at slow speeds, the price of the hold).
Through the input pass (`eyesSteady`) the dot on the cog does not move at all on the same jitter, where the head's
filter wanders 2 degrees. The dwell allows the eyes 2 degrees of wander (`accessDwellToleranceDeg`), the head 1; the
radial menu's dead zone (2.5) is still wider. What it cannot fix is the offset: the gaze is not exact, so the dot lands
near where the user looks. Numbers to tune in the headset: the hold radius (larger is steadier and less precise) and
the smoothing.

## Eye calibration (lane eye-calibration)

A fifth row, **Eye calibration** (`COG_ACCESS_ROW_EYECAL`), with Start and Reset. It is live only while eye gaze
reaches the app (`accessEyeCalLive`: `ACCESS_EYES_READY`), whatever points; otherwise its cells' room shows the
eye tracking line's text, and the line under the rows (now at `COG_ACCESS_STATUS_V` 0.555, the help at 0.615) is
drawn only while it says Available. Under the row's name: what `[vr] eye_calibration` holds ("Calibrated: 0.9°
average") or how this session's last run failed (`UiView.access_cal`).

| Where | What |
| --- | --- |
| `xr/core/xr_eyecal.c` | Pure. The 4 x 3 grid inset 8 % (`eyeCalTarget`) and its order; the run (`EyeCalRun`: 0.4 s to settle, 0.8 s read; a fixation is the samples within 3° of their median, at least 60 % of the frames, a spread under 1.5° RMS and within 15° of the dot; a dot that fails is tried once more, then skipped); the fit; the `config.ini` form. `tests/xrcore/test_eyecal.c` |
| `xr/xr_assist.c` | The row's presses, the run's frame (`xrEyeCalFrame`: the fit put in force exactly as stored, `SETTING_EYE_CAL` 47 handed back on a free slot), the dot's point and size, and the correction (`xrEyeCalCorrect`) |
| `xr/xr_input.c` | `readGazeValid` keeps the raw gaze (`gazeRaw`); `readSources` corrects the gaze right after it is located, before the hold, the steadying and the smoothing, so the Eyes pointer, Look to point, the dwell and a gaze grab all get it; one test of `eyeCal.model` when none is stored. `updateEyeCal` takes the cog's place while a run is up: the pointer on nothing, no dwell, no bindings (let go of as the run starts and ends), B or Menu cancels, and the panel closing or focus lost cancel |
| `xr/xr_layers.c` | `addEyeCalLayers`: the cog's two layers are the label (the cog's art drawn as `eyecal_face`, at the picture's middle, no wider than 0.36 of it) and the dot (`ATLAS_DOT`, 1.2° shrinking from 3°), and the pointer's beam, dot and dwell mark hold their places, so the count and the beam's index are those of the cog with the pointer on it (`tests/m4/test_kbbar.c` `eyeCalKeepsBeamPlace`) |
| `tests/m4/test_eyecal.c` | Through the seams: a gaze off by an offset and a scale, the head turning, follows the dots; stored, read back, applied; cancels; a failed run keeps the old; Reset |

The fit is in the head's frame (yaw and pitch of the gaze, and of the dot from the gaze's origin, both at the
frame's head orientation): true = measured + c0 + c1 u + c2 v, per axis, u and v the measured angles over 10°. The
worst dot is dropped (up to 3, never below 8) while its error is over 2.5 times the median and over 1°. With 10 or
more dots a second-order term (u², uv, v²) is tried and kept only when its leave-one-out error is under 0.8 of the
affine's and 0.15° less. Refused: under 8 dots; a mean error after above 0.8 of the one before or less than 0.2° better;
a scale (singular values of the Jacobian, at the middle and, for the second order, over ±30° by ±20°) outside 0.7 to 1.4
or a mirror; an offset at the middle over 10°. The errors are those of the dots used, so the "after" is the fit's own
and reads a little better than a fresh look would; the headset is the judge.

## Future: the headset button

The owner, for 1.1.0: "forget it for now; list it as a future thing". Lane a11y-2 built it (below, unchanged); lane
a11y-final took it out of reach for 1.1.0:

- The Accessibility tab's Headset button clicks row is gone, and the launcher's (its strings and `MAP` lines too, but
  `UIS_AUX_RETURNED`, the 3 s hold's toast, which the code below still says).
- `[vr] assist_headset_button` and `[vr] assist_click_path` stay keys of `config.ini` (`ui/settings.c`; an old file
  reads without a warning, and the launcher keeps a file's value as it is), undocumented in the user guide, and
  ignored: `xr/xr_prefs.c` reads neither, so `ctx->assistHeadsetButton` is never on and `ctx->assistClickPath` is
  empty. `xrAuxButtonFrame` then never looks for the device, never opens or grabs it, and no OpenXR button is made or
  read. Only the tests turn `ctx->assistHeadsetButton` on (`tests/m4/test_access.c` `auxGrab`, which also checks that
  `config.ini`'s keys turn nothing on), so the code keeps working.
- A press no longer lets go of the radial menu's drag (`xrDwellDragRelease` is gone); the next dwell does.
- `mlxr-hello --button-probe` is unchanged.

To bring it back: read the two keys in `xrApplySettings` again, give the tab its row back (`COG_ACCESS_ROW_BUTTON`,
`cogApplyAccessCell`'s case, the greyed reasons `UIS_BUTTON_MISSING` and `UIS_BUTTON_UNREADABLE`, the footer's
`UIS_ACCESS_DESC_BUTTON` and `UIS_ACCESS_DESC_BUTTON_UNREADABLE`, `UiView.access_button` from `ctx->auxState`; all in
lane a11y-2's commits), and the launcher's row (`assistHeadsetButtonRow`; `MlxrPrefs.assistHeadsetButton` is still
there). The tab has the room: the eye line and the instructions moved up into it.

What the user guide said about it (for when it comes back):

> **The headset's side button.** With **Headset button clicks** on and the head or the eyes pointing, the button on the right side of the headset (the one that normally opens the Steam home) clicks where the dot is:
>
> | Press | What it does |
> | --- | --- |
> | Once | A left click |
> | Twice, quickly | A double click |
> | Three times, quickly | A right click |
> | Hold (longer than 0.4 s) | Holds the left button down to drag; let go to drop |
> | Hold 3 seconds | Gives the button back to SteamVR for 15 seconds (a message says so), so the next press opens the Steam home as usual. A drag under way is let go first. |
>
> While it clicks, the app holds the button for itself, so SteamVR does not see it. It gives the button back whenever the app loses focus (the Steam home or the dashboard is up), the stream ends, a controller points, you turn the row off or the app closes; if the app stops for any reason, the system gets the button back by itself. A single click waits about a third of a second to see whether a second press follows.
>
> The app reads the button from its input device (`gpio-keys`, the device Linux reports the button on). It never changes permissions itself. If your user may not read that device, the row is greyed with "cannot read the button" and `moonlight.log` says `headset button: ... not readable`. What you can do, if you choose to (an advanced, optional step, at your own risk): give your user read access to that one device with a udev rule, for example a file `/etc/udev/rules.d/70-mlxr-headset-button.rules` holding `SUBSYSTEM=="input", ATTRS{name}=="gpio-keys", TAG+="uaccess"`, then reboot. This lets programs you run read the headset's volume and side buttons; it changes nothing else, and deleting the file undoes it. Making the file needs root (`sudo`); a SteamOS update may undo it.
>
> `mlxr-hello --button-probe` (below) says whether the button reaches the app on your headset.

### How it was built (lane a11y-2)

The Steam Frame's side button (Valve calls it the Aux button; SteamVR opens its dashboard, the Steam home, with it).
Valve documents no OpenXR path for it. Linux reports it as `KEY_SELECT` on the `gpio-keys` input device, and a
community tool keeps it from SteamVR by taking that key; this app does the same for the head and eye pointer.

| Where | What |
| --- | --- |
| `xr/core/xr_assistclick.c` | Pure. `AuxClick`, the gestures: a press a left click, two within 350 ms a double click, three a right click, held past 400 ms the left button down (a drag) until let go, held 3 s `ASSIST_ESCAPE` (the drag let go). `AssistPlay`, what an action does to the mouse buttons frame by frame (60 ms presses; a double click two of them 60 ms apart; a drag held). `tests/xrcore/test_assistclick.c`, table driven |
| `input/auxbutton.c` | The device: found through `/sys/class/input/event*/device/{name,capabilities/key}` by the name `gpio-keys` and the `KEY_SELECT` bit (never by event number), opened read-only and non-blocking, grabbed with `EVIOCGRAB`, read and parsed. `tests/input/test_auxbutton.c`: a made-up sysfs tree, an unreadable device, events through a pipe, the grab on open and off on close |
| `xr/xr_auxbutton.c` | Once a frame before the input pass (`xr_session.c`): takes the button (grab) only while the row is on, a head or eye pointer is chosen, the session is focused, a stream is up and gamepad mode is off; gives it back the moment any of that stops, at the session's end, and for 15 s after a 3 s hold (toast `UIS_AUX_RETURNED`). The kernel drops the grab when the process ends, so a crash cannot keep it. Feeds the gestures and plays them (`xrAssistFrame`): `ctx->assistButtons` go to the host as mouse buttons on the picture (`updateButtons`) or to our own menus as a press (`gazeTrigger`, a double or right click there is one press), decided when the action starts from what the dot is on |
| `[vr] assist_click_path` | The same gestures from an OpenXR button, as `<interaction profile> <input path>`, empty by default. `xrAssistClickBind` adds it to the first rung of the app's own suggestion for that profile (so a refusal costs the next rung nothing), `xrAssistClickSuggestAlone` suggests it on its own for a profile the app does not use, `xrAssistClickRead` reads it after each sync. For whatever path `--button-probe` reports, without a rebuild |
| `tests/m4/test_access.c` `assistClicks`, `auxGrab` | Clicks on our menus and on the picture, a drag; the grab taken and given back (focus lost, setting off, the 3 s hold and the 15 s after it) with a FIFO standing in for the device and a fake grab |

Permissions: the app never asks for more. When the device is not readable (`EACCES`), the log says so once, the row
is greyed with "cannot read the button: see the guide" and its footer sentence says the user guide has the steps. The
user guide offers an optional udev rule (`TAG+="uaccess"` on `ATTRS{name}=="gpio-keys"`) with its risk stated; the app
installs nothing.

### Does the button reach the app? The button probe

```sh
~/Games/MoonlightXRNative/src/build/hello/mlxr-hello --button-probe          # read-only, about 20 s once focused
~/Games/MoonlightXRNative/src/build/hello/mlxr-hello --button-probe --grab   # takes the device for the first 10 s only
```

With SteamVR up, the client closed and the headset worn; during the watch press the side button once, twice, and
hold it, and note what the headset did. It suggests a boolean action on every path that could be the button (the
Frame controller profile's `system/click` on both hands and guesses under `/user/head` such as `system`, `aux`,
`home` and `dashboard`, the same under `frame_controller_valve`, `htc/vive_pro`'s `/user/head/input/system/click`,
the Index, Vive and Touch controllers' system and menu buttons, `khr/simple_controller`'s menu), a profile at a time
(a profile refused whole is tried a path at a time, and the log says which paths the runtime took), prints every
change with its path, the interaction profiles bound to `/user/head` and both hands, and the session's state: the
Steam home opening takes the focus, so a press SteamVR keeps for itself still shows as "LOST FOCUS at t". It also
looks for the evdev device and says whether it is there and readable, and prints its `KEY_SELECT` presses (without
`--grab` SteamVR still sees them, and says so). The summary's lines:

- `RESULT OpenXR: (a)`: the app receives presses on a path and the Steam home did not open. Put that path in
  `assist_click_path`.
- `RESULT OpenXR: (b)`: the app receives presses, but the Steam home also opens.
- `RESULT OpenXR: (c)`: the runtime keeps the button: no path delivered a press, the focus was lost.
- `RESULT evdev:`: whether the device is there and readable, how many presses it saw, and with `--grab` whether the
  focus held during the grab (SteamVR did not act on the presses).

Exit status 0 when presses reached the app by either way, 3 when none did, 1 on an error.

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

## Design and research: the dwell's click types (the radial menu)

The owner asked for a dwell that can also double click, right click and drag, and for the dwell's square ring to
become a circle. Lane radial-dwell built it (2026-10-30); the survey that led to it comes first, then what was built
and its numbers.

**What others do** (survey 2026-10-30; links as found):

- Windows Eye Control: dwell on the launchpad's Left or Right click to arm, then dwell on the target; Precise mouse
  adds Double and Close; no drag. https://support.microsoft.com/en-US/accessibility/windows/eye-control/eye-control-basics-in-windows
- Tobii Dynavox TD Control: one interaction menu (Double, Left, Scroll, Right, Click and drag): look at the target,
  then pick the action. https://learn.tobiidynavox.com/catalog/info/id:326,
  https://www.tobiidynavox.com/blogs/support-articles/what-selection-options-are-available-in-windows-control
- macOS Dwell Control: Left Click by default, Double, Right, Drag and Drop from an Options menu, one shot and back to
  Left; a pointer circle that empties as the countdown. https://support.apple.com/guide/mac-help/mchl437b47b0/mac
- GNOME Hover Click (Mousetweaks): a window picks the click type, back to normal after double, right or drag; the
  button fills with colour; Mousetweaks once had a gesture mode where the direction moved after the dwell picks the
  click (now unmaintained). https://help.gnome.org/gnome-help/a11y-dwellclick.html, https://www.mankier.com/1/mousetweaks,
  https://wiki.gnome.org/Projects/Mousetweaks
- eViacam: dwell is a left click; its gesture click is dwell, then move the head up, down, left or right and stop
  again, and that direction's action fires, with a progress bar by the pointer. The closest shipped design.
  https://svnlliurexnet.gva.es/focal/eviacam/trunk/fuentes/doc/help/en/click.htm
- OptiKey: pick the action first, then the point; drag is two explicit points. https://github.com/Optikey/Optikey/wiki
- HoloLens head-gaze and dwell: a short delay before feedback, a radial fill from the target's centre, targets of at
  least 2 degrees. https://learn.microsoft.com/en-us/windows/mixed-reality/design/gaze-and-dwell-head
- Pie and gaze menus: border crossing is faster and makes fewer unintended picks than dwell
  (https://www2.cs.sfu.ca/~stella/papers/2008/etra.pdf); Lattice Menu's numbers for gaze item zones (about 4 degrees,
  labels at least 3 degrees from a border) (https://arxiv.org/html/2511.21131); gaze marking menus suffer saccade
  overshoot (StickyPie, https://research.fb.com/publications/stickypie-a-gaze-based-scale-invariant-marking-menu-optimized-for-ar-vr/);
  direction gestures reduce but do not end false picks (https://bop.unibe.ch/JEMR/article/download/2384/3580/8608).
- Dwell times: 500 to 600 ms most common, about 300 ms for practised users; false clicks rise as the dwell shortens
  (https://homepages.tuni.fi/oleg.spakov/publications/Majaranta_COGAIN_09.pdf).

**Options considered.** A click-type window or launchpad (Windows, macOS, GNOME): proven, but two dwells a click and a
mode to forget. Pick the action first (OptiKey): robust drags, slow for the common left click. A menu at the end of
the dwell (eViacam's gesture, Mousetweaks' gesture mode): the left click stays one dwell, the others one glance more.
The owner's request is the last; it costs nothing to someone who only wants left clicks.

**What was built.** The dwell's ring is a circle: a thin arc in the dot's colour (white, with a soft dark rim so it
shows on a bright picture) sweeps clockwise from the top over a faint full circle while the circle shrinks from
0.86 of its quad's half side to just round the dot (0.27, the dot's edge being 0.2); the click fires as it meets the
dot. It shows from 10 % of the dwell on (a dot passing over things does not flicker circles), and a dwell dropped
before its click fades out over 150 ms. It is the head and eye pointer's only (the controllers' dwell-less dot is
unchanged) and scales with Dot size: the dot is drawn into every picture of the circle, so the two are one quad of
three times the dot's side.

A **Dwell click style** row (`[vr] assist_dwell_style = simple | radial`, Simple by default; cog and launcher, with
the footer sentence and the launcher tip "Radial menu: near the end of the dwell, look left for double click, down to
drag, right for right click.") picks the radial menu. It acts on the picture only: a dwell on our own menus stays a
plain press. From 75 % of the dwell a menu fades in round the point the dwell started on: three segments outside a
dead zone, left a double click, down a drag, right a right click, up an empty gap. Keeping the dot in the middle (or
in the gap) until the dwell ends is a left click; moving it into a segment and holding it there picks that segment
(it lights from its inner edge outward as the hold runs); past the outer edge the dwell is cancelled. Every click
lands on the dwell's start: the host's cursor is held there while the menu is up and while the chosen click's presses
play, and the buttons count there even when the dot has left the picture to pick. A drag holds the left button from
the start; the cursor then follows the dot, the dot is drawn hollow and a small mark stays on the start; the next
dwell anywhere (no menu while a drag is held) lets go there, a dwell back inside the dead zone round the start lets
go on the start itself (nothing moves). (Until lane a11y-final a press of the headset button let it go too; that went
with the button.) Losing focus, the stream or the head pointer lets a drag go too.

**The numbers** (`xr/core/xr_radial.h`; to tune in the headset): menu from 75 % of the dwell; dead zone 2.5 degrees
round the start, a segment held until the dot is back inside 2.2 (0.3 degrees of slack, so a dot resting on the edge
does not restart the hold); the segments' outer edge drawn at 4.5 degrees and the cancel edge at 1.5 times that, 6.75
(the overshoot room the research asks for: a dot past the drawn segment but inside 6.75 still picks it); four quarters
with their edges on the diagonals, up the gap; a segment's hold 250 ms; the menu's fade in 120 ms in three steps while
it grows from 92 % to its full size. The menu is drawn 4.8 degrees round its middle whatever the Dot size; at 300 % the
circle's start (1.6 degrees) is still inside the dead zone.

| Where | What |
| --- | --- |
| `xr/core/xr_radial.c` | Pure. `radialStep` over the dwell (`xr_dwell.c`): opens the menu at 75 %, then the dead zone, the segments and their hold, the outer cancel, the left click at the end; leaves the dwell spent where the dot is after a choice. `radialSectorAt`, and the art's frames (`radialRingFrame`, `radialFadeFrame`, `radialMenuFrame`). `tests/xrcore/test_radial.c`, table driven |
| `xr/xr_dwellmenu.c` | The XR side. `xrDwellMenuStep` from `updateDwell` (`xr/xr_input.c`): the dwell's start on the picture (u, v and the point in the space), the cursor held there (`xrDwellMenuHostPoint`, used by `sendPointer`, and for the buttons' `hit`), the choices played through `xrAssistActionAt` (`xr/xr_auxbutton.c`, the headset button's player), the drag and its release, the circle's fade. `xrDwellMenuArt` picks the frames for `xr/xr_layers.c`; `xrDrawDwellArt` draws them all once into the atlas with the rest of the code drawn art |
| `xr/xr_layers.c` | The dot's quad shows a frame of the circle (or the hollow dot); the menu, or a drag's mark, is one more quad at the dwell's start (`dwellMark`, under the dot). The head and eye pointer never draw the beam, so the busiest frame is no fuller (`tests/m4/test_budget.c`) |
| Atlas | `ATLAS_DWELL_RING` (64 px frames: 40 steps of the sweep, 5 x 3 fades, the hollow dot, the mark) under the Ko-fi sheet, `ATLAS_DWELL_HOLLOW` (20 steps round a hollow dot) right of the pad card, `ATLAS_DWELL_MENU_A` to `C` (15 frames of 160 px: 3 of the fade in, 4 of each segment's hold) in three free corners |
| `tests/m4/test_access.c` | `stylePresses`, `radialMenu`, `radialDrag`: through the input pass's seams, the row's press, a dwell on the tab staying a press, staying, each direction, jitter and the gap, the cancel, the simple style, the cursor held on the start, the drag's start, its release by a dwell, on its start and on focus loss, and the quads the frame shows |
| Goldens | `31-dwell-ring-25`, `31b-dwell-ring-50`, `31c-dwell-ring-75` (Dot size 300 %), `32-dwell-radial-menu` (right segment lit, the dot on it) |

## Later: eye gestures (not built)

The plan's closed-eyes click and pause (`assist_eye_gestures`, `assist_close_click_ms`, `assist_close_pause_ms`) wait
for the probe's answer on whether the gaze pose loses its tracked bit while the eyes are closed, and after how long
(the Frame does give the app eye gaze, see above; the closed-eyes lines of that run are still to be read). The Frame gives no per-eye openness, so closing both eyes is
the only gesture there can be. When it is built: feed `xrGazeValid()` (and `ctx->gazeFlags`) and the clock to a pure
timer in `xr/core` (nothing under 500 ms, a blink; the click at 1 s and more, the pause at 2.5 s), freeze the dot the
moment the gaze goes invalid, keep it off by default and behind its own switch, and keep the dwell working beside it.
The Action strip, Pause and Resume, snapping and the assist keyboard of the plan are not built either.
