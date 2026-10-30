# Controller bindings: `bindings.ini` and `xr/core/xr_bind`

PLAN.md Wave 2 row 2.5 (research report 15, section a). This is the binding core: a pure module (no OpenXR, no GL, no context) that reads `~/.config/moonlight-xr/bindings.ini`, layers it over built in defaults and answers "what does this control do now". The input pass uses it (lane wave2-bind-wire, "How the input pass uses it" at the end). Tests: `tests/xrcore/test_bind.c` (`xrcore_bind`), `tests/xrcore/test_bindrun.c` and `tests/m4/test_bindings.c`.

## The file

Where: `$MLXR_CONFIG_DIR/bindings.ini`, else `~/.config/moonlight-xr/bindings.ini` (`xrBindDefaultPath`), next to `config.ini`. The first run writes the default file below (`xrBindLoadFile` with no file there), in which every default is a commented out line, so the file changes nothing until a line is uncommented.

```ini
[pointer]                      ; pointer mode, every app
right.x = key:F
left.stick_left = key:ctrl+shift+esc
left.dpad_left+right.x_touch = wheel:down   ; a chord

[gamepad]                      ; gamepad mode, every app
left.grip = none
right.grip = key:R
left.view+right.menu = none    ; no Guide chord
right.a+right.b = pad:guide

[app "Time Crisis 2"]          ; one host app, over the two above
pointer.right.trigger = mouse:left
gamepad.right.grip = pad:rb
```

- **Sections.** `[pointer]` and `[gamepad]` are the two modes for every app. `[app "Name"]` is one host app, its name matched case blind against `Settings.app` (the name the app list shows). Lines in an app section start with the mode: `pointer.` or `gamepad.`. A section may appear more than once; its lines add up.
- **Layering** (`xrBindResolve(set, mode, app)`): the defaults, then the mode's section, then the app's lines for that mode. A later layer replaces a control (or a chord with exactly the same controls); a chord bound to `none` is removed.
- **Lines.** `control = action`, or a chord `control+control[+control[+control]] = action` (two to four controls, all held together). Spaces are free around `=` and `+`. Names are case blind. `#` or `;` starts a comment, also after a line. A UTF-8 byte order mark and `\r\n` line ends are accepted.

### Controls (sources)

| Hand | Controls |
| --- | --- |
| left | `trigger`, `grip`, `bumper`, `stick`, `stick_up`, `stick_down`, `stick_left`, `stick_right`, `stick_click`, `view`, `dpad_up`, `dpad_down`, `dpad_left`, `dpad_right`, `trigger_touch`, `stick_touch` |
| right | `trigger`, `grip`, `bumper`, `stick`, `stick_up`, `stick_down`, `stick_left`, `stick_right`, `stick_click`, `a`, `b`, `x`, `y`, `menu`, `trigger_touch`, `stick_touch`, `a_touch`, `b_touch`, `x_touch`, `y_touch` |

Written `left.trigger`, `right.a_touch` and so on. `left.stick` and `right.stick` are the whole stick (two axes) and only take `stick:` actions or `none`; the four directions and the click are buttons. The touch controls are the capacitive sensors in the OpenXR probe (`docs/probe/openxr-probe.txt`); only the right thumbstick's touch is bound to an OpenXR action so far, and only for the pad gyro's `stick_touch` ratchet (`xr/xr_input.c`, `suggestFrameBindings`); the rest are not bound yet (that needs a headset check that the Frame profile accepts them all together), and a rule on any touch control, `right.stick_touch` included, still reads nothing. The system button is SteamVR's and is refused with its own message.

### Actions

| Action | Meaning |
| --- | --- |
| `key:<key>` | A keyboard key, by name or VK. Modifiers come first: `key:ctrl+shift+esc`, `key:alt+F4`, `key:win+D`. Names: letters, digits, `F1`-`F24`, `esc`, `tab`, `enter`, `space`, `backspace`, `delete`, `insert`, `home`, `end`, `pageup`, `pagedown`, `up`/`down`/`left`/`right`, `shift`/`ctrl`/`alt`/`win` (and `l`/`r` forms), `apps`, `capslock`, `numlock`, `scrolllock`, `printscreen`, `pause`, `minus`, `equals`, `lbracket`, `rbracket`, `backslash`, `semicolon`, `quote`, `grave`, `comma`, `period`, `slash`, `num0`-`num9`, `multiply`, `add`, `subtract`, `decimal`, `divide`, `volumeup`, `volumedown`, `mute`, `nexttrack`, `prevtrack`, `stop`, `playpause`, `browserback`, `browserforward`. Any Windows VK as `0x41` or `vk65`. Modifier bits are Moonlight's `MODIFIER_` bits (shift 1, ctrl 2, alt 4, win 8), the same as `KB_MOD_` |
| `mouse:left\|right\|middle\|x1\|x2` | A host mouse button. Code n is the `VR_BUTTON_` bit `1 << n` (1, 2, 4, 8, 16); `xrBindHostButton` gives Moonlight's `BUTTON_` (1, 3, 2, 4, 5), the old 3-button `MAP` widened to 5 |
| `wheel:up\|down\|left\|right` | One wheel notch per press |
| `pad:<button>` | `a b x y lb rb lt rt ls rs start back guide dpad_up dpad_down dpad_left dpad_right`. Gamepad mode only. Codes are the `PAD_` bits; `lt`/`rt` are `PAD_HELD_LT`/`RT` (a trigger bound to them stays analogue, a button pulls them full) |
| `stick:<use>` | `scroll mouse wasd arrows dpad pad_left pad_right`, whole sticks only. `pad_left`/`pad_right` are gamepad mode only |
| `app:<command>` | `keyboard` (show or hide), `ray` (step Off/Dot/Ray), `recentre` (what SteamVR's recentre does: the screen comes round to where you face), `cog` (open or close the cog panel), `3d` (on or off), `gamepad` (switch modes at once, no hold), `bringhere` (the screen comes to where you look, the same size and as far from you as it was; built in PLAN 3.1, `xrBringScreenHere` in `xr/xr_place.c`), `host` (open or close the Host sheet, keys for the PC), `zoomin` / `zoomout` (one zoom rung, aimed at the pointer) and `zoomreset` (the whole picture; PLAN 4b.6, `xrZoomStep` in `xr/xr_zoom.c`), `lookhold` (held, head-look pauses; nothing on the press. Lane wave4-headinput, PLAN 4.4: a grip bound to nothing pauses it too), `none` |
| `none` | Nothing |

### Never bindable (refused with an error, or kept working whatever is bound)

| What | How |
| --- | --- |
| The pointer/gamepad switch shortcuts: `left.view+left.grip`, `left.stick_click+right.stick_click`, `left.trigger+right.trigger+left.grip+right.grip` | A chord equal to any of the three is refused in every mode, whichever one `gamepad_toggle` picks, so changing that setting never turns a binding into a clash. Their controls stay bindable one by one: the shortcut logic in `xr_gamepad` runs first and holds its parts back |
| The disconnect chord of gamepad mode, `left.view+right.menu+left.bumper+right.bumper` (Start, Back, LB, RB) | A chord that holds all four is refused. In gamepad mode `xrBindHeld` sends the four held together as Start, Back, LB and RB whatever they are bound to, and a chord made only of those controls (Guide by default) gives way while another of them, or anything bound to LB or RB, is down, as `padGuideStep` does |
| Grip grab over our UI | In pointer mode a chord with a grip in it is refused (half of it would grab). The grips' own bindings only apply off our UI: the caller takes them out of the controls it hands in while the ray is on the move pill, a corner, the keyboard's bar or the stats window |

### Errors

The parser is strict but never fatal: a bad line is left out with an error and every other line still counts. Each error is `line:column: what` (1 based, the column of the offending name or value). At most 16 are kept, all are counted (`XrBindErrors.count`). Refused:

- an unknown section, control, action kind, key, mouse button, wheel direction, pad button, stick use or app command (each with the list of what is allowed);
- a line before any section, without `=`, with nothing on one side, longer than 512 characters;
- in an app section a line without `pointer.`/`gamepad.`, and that prefix outside one;
- the same control or chord twice in one section and mode ("already bound on line N"; the first stands), a control twice in a chord, a chord of more than four or with a whole stick in it, more than 8 chords per section and mode, more than 256 lines or 32 apps;
- `pad:` (and `stick:pad_left`/`pad_right`) in pointer mode, a `stick:` action on a button, a button action on a whole stick;
- the reserved chords above, and `left.system`/`right.system`.

## The defaults

Today's fixed maps (USER_GUIDE "Controllers", `pointerButtonMask` in `xr_pressfix.c`, `padMapGrips` in `xr_gamepad.c`), plus the seven controls pointer mode left idle:

| Control | Pointer mode default |
| --- | --- |
| Y | `app:keyboard` |
| X | `app:3d` |
| d-pad up | `app:recentre` |
| d-pad left | `none` (PLAN 3.5 names no default for it) |
| menu (right) | `app:cog` |
| left bumper | `mouse:x1` (back) |
| right bumper | `mouse:x2` (forward) |

`test_bind.c` checks that the pointer defaults give exactly `pointerButtonMask`'s buttons over every combination of the triggers, A, B, d-pad down/right and stick clicks, and that the gamepad defaults give exactly `padMap`'s buttons over all 65536 combinations of the Frame buttons and grips (with Guide where `padGuideStep` would give it once held).

The whole default file (`xrBindDefaultText`; `test_bind.c` checks this block against it byte for byte):

<!-- bindings.ini default: begin -->
```ini
# Moonlight XR controller bindings: what each Frame control does.
#
# Every line below is a default, commented out, so this file changes nothing
# until a line is uncommented (take the # off) and edited. A line is
#   control = action
# or a chord of two to four controls held together:
#   left.view+right.menu = pad:guide
#
# Actions:
#   key:F  key:space  key:ctrl+shift+esc  key:0x41   a keyboard key, modifiers first
#   mouse:left|right|middle|x1|x2                    a mouse button (x1 back, x2 forward)
#   wheel:up|down|left|right                         one wheel notch per press
#   pad:a|b|x|y|lb|rb|lt|rt|ls|rs|start|back|guide|dpad_up|dpad_down|dpad_left|dpad_right
#                                                    a gamepad button (gamepad mode only)
#   stick:scroll|mouse|wasd|arrows|dpad|pad_left|pad_right
#                                                    a whole stick (left.stick, right.stick)
#   app:keyboard|ray|recentre|cog|3d|gamepad|bringhere|host|zoomin|zoomout|zoomreset|lookhold
#   none                                             nothing
#
# [pointer] is pointer mode and [gamepad] gamepad mode, for every app. An
# [app "Name"] section is for one host app (its name as the app list shows
# it) and goes over those two; its lines say the mode first:
#   [app "Time Crisis 2"]
#   pointer.right.x = mouse:right
#   gamepad.left.grip = none
#
# Never bindable: the pointer/gamepad switch chords (left.view+left.grip,
# left.stick_click+right.stick_click, both triggers with both grips), the
# disconnect chord (left.view+right.menu+left.bumper+right.bumper), a chord
# with a grip in pointer mode, and the grips' grab while the ray is on our UI.
# A line that is not right is left out and moonlight.log says which and why.

[pointer]
#left.trigger = mouse:left                ; left click, hold to drag
#left.grip = none                         ; over our UI it grabs, whatever it is bound to
#left.bumper = mouse:x1                   ; back
#left.stick = stick:scroll                ; scroll
#left.stick_up = none
#left.stick_down = none
#left.stick_left = none
#left.stick_right = none
#left.stick_click = mouse:right           ; right click
#left.view = none                         ; held with the left grip: pointer/gamepad switch
#left.dpad_up = app:recentre              ; the screen comes round to where you face
#left.dpad_down = mouse:right             ; right click
#left.dpad_left = none                    ; free
#left.dpad_right = mouse:middle           ; middle click
#left.trigger_touch = none
#left.stick_touch = none
#right.trigger = mouse:left               ; left click, hold to drag
#right.grip = none                        ; over our UI it grabs, whatever it is bound to
#right.bumper = mouse:x2                  ; forward
#right.stick = stick:scroll               ; scroll
#right.stick_up = none
#right.stick_down = none
#right.stick_left = none
#right.stick_right = none
#right.stick_click = mouse:right          ; right click
#right.a = mouse:right                    ; right click
#right.b = mouse:middle                   ; middle click
#right.x = app:3d                         ; 3D on or off
#right.y = app:keyboard                   ; show or hide the keyboard
#right.menu = app:cog                     ; open or close the cog panel
#right.trigger_touch = none
#right.stick_touch = none
#right.a_touch = none
#right.b_touch = none
#right.x_touch = none
#right.y_touch = none

[gamepad]
#left.trigger = pad:lt                    ; LT
#left.grip = pad:lb                       ; LB; config.ini's pad_grip while left alone
#left.bumper = pad:lb                     ; LB
#left.stick = stick:pad_left              ; left stick
#left.stick_up = none
#left.stick_down = none
#left.stick_left = none
#left.stick_right = none
#left.stick_click = pad:ls                ; LS
#left.view = pad:back                     ; Back
#left.dpad_up = pad:dpad_up               ; d-pad
#left.dpad_down = pad:dpad_down           ; d-pad
#left.dpad_left = pad:dpad_left           ; d-pad
#left.dpad_right = pad:dpad_right         ; d-pad
#left.trigger_touch = none
#left.stick_touch = none
#right.trigger = pad:rt                   ; RT
#right.grip = pad:rb                      ; RB; config.ini's pad_grip while left alone
#right.bumper = pad:rb                    ; RB
#right.stick = stick:pad_right            ; right stick
#right.stick_up = none
#right.stick_down = none
#right.stick_left = none
#right.stick_right = none
#right.stick_click = pad:rs               ; RS
#right.a = pad:a                          ; A
#right.b = pad:b                          ; B
#right.x = pad:x                          ; X
#right.y = pad:y                          ; Y
#right.menu = pad:start                   ; Start
#right.trigger_touch = none
#right.stick_touch = none
#right.a_touch = none
#right.b_touch = none
#right.x_touch = none
#right.y_touch = none
#left.view+right.menu = pad:guide         ; held a quarter second; gives way to the disconnect chord
```
<!-- bindings.ini default: end -->

## The API (`xr/core/xr_bind.h`)

```c
// Names
const char* xrBindSourceName(int src);              // "right.y"
int xrBindSourceFind(const char* name);             // XR_BIND_SRC_ or -1
int xrBindSourceIsStick(int src);
int xrBindKeyFind(const char* name);                // VK or 0
const char* xrBindKeyName(int vk);                  // or NULL
int xrBindHostButton(int mouseCode);                // Moonlight BUTTON_ 1,3,2,4,5
int xrBindActionParse(const char* text, XrBindAction* out, char* err, size_t cap, int* col);
int xrBindActionFormat(const XrBindAction* a, char* out, size_t cap);
int xrBindActionEqual(const XrBindAction* a, const XrBindAction* b);

// The file
void xrBindSetInit(XrBindSet* s);
int xrBindParse(XrBindSet* s, const char* text, XrBindErrors* errs);      // returns errors
int xrBindSerialize(const XrBindSet* s, char* out, size_t cap);           // snprintf style
int xrBindDefaultText(char* out, size_t cap);                             // snprintf style
const char* xrBindDefaultPath(char* buf, size_t cap);
int xrBindLoadFile(XrBindSet* s, const char* path, XrBindErrors* errs);   // 1 wrote default, 0 read, -1 failed

// The table
void xrBindResolve(const XrBindSet* s, int mode, const char* app, XrBindTable* out);
uint64_t xrBindSourcesDown(const PadHand* left, const PadHand* right, uint64_t was);
int xrBindHeld(const XrBindTable* t, uint64_t down, XrBindAction* out, int cap);
int xrBindMouseMask(const XrBindTable* t, uint64_t down);   // VR_BUTTON_ bits incl. X1 8, X2 16
int xrBindPadButtons(const XrBindTable* t, uint64_t down);  // PAD_ bits
```

`XrBindSet` is about 12 KB and `XrBindTable` about 1 KB; keep the set in the context or a static, not on a small stack.

## How the input pass uses it

Lane wave2-bind-wire. `xr/core/xr_bindrun.h` is the table at work frame to frame (pure, `tests/xrcore/test_bindrun.c`); `xr/xr_input.c` ("bindings.ini at work") feeds it; `tests/m4/test_bindings.c` drives the whole thing through `xrBindFrame`.

- **Start.** `xr_session.c` calls `xrBindLoad(ctx, NULL)` after the settings: the file is read (written the first time), each error goes to the log as `bindings: <file>:<line>:<col>: what` (a warning), the toast says `bindings.ini: N errors, see log` (a message, so Notices Off silences it), and one line says what differs from the defaults (`bindings: pointer mode right.y = key:F; gamepad mode as the defaults`, or `bindings: the defaults`).
- **The host app.** `app/main.c` names the app when a stream starts (`xr_app_set_host_app`, the name from the app list, or `--app` when that is a name) and clears it when the stream stops; the frame loop hands it to `xrBindSetApp`, which lets go of whatever is held and resolves both tables again.
- **Pointer mode** (`bindPointerStep`, after the panels have taken their presses): the controls are `xrBindSourcesDown` over the pad's view of the controllers (after the switch shortcut's hold-back), with the triggers replaced by `triggerDown` less `triggerSwallowed` (press stabilisation and presses spent on our UI), the grips taken out while a grab is on or the ray is on the move pill, a corner, the keyboard's bar or the stats window, and the stick clicks taken out while they are at their default and `MLXR_STICK_RIGHT_CLICK=0`. `xrBindEdgesMouse` replaces `pointerButtonMask` (a hand without a controller still pinches the left button); `dispatchInput` maps five bits through `xrBindHostButton`. Left View stays unbound.
- **Gamepad mode** (`bindPadStep`, in `updatePad`): while the table is the defaults (`xrBindTableSame`), `padMapGrips` and `padGuideStep` make the pad exactly as before. Once it differs, the buttons come from the table (`xrBindEdgesPad`), a trigger on its own `pad:lt`/`pad:rt` stays analogue and a button on one pulls it all the way, sticks on `stick:pad_left`/`pad_right` go through `padStick`, grips at their default follow `pad_grip`, and the View+Menu Guide chord keeps its quarter-second hold through `padGuideStep` while it is still `pad:guide` over Back and Start (`xrBindTableGuideHold`); bound to anything else it fires at once. A panel up (or no controller held) lets go of everything and lets only `app:` commands through.
- **Edges.** `key:` goes down and up through `XrAppHooks.bindKey` (`app/main.c` presses the modifier keys around it); `wheel:` adds a notch on the press; `app:` runs on the press (`keyboard`, `ray` steps Off/Dot/Ray and saves it as the Display tab does, `recentre` brings the screen round to the head's heading with `poseRecentredTo`, `cog`, `3d`, `gamepad`, `bringhere` through `xrBringScreenHere`, `host` opens or closes the Host sheet, `zoomin`, `zoomout` and `zoomreset` through `xrZoomStep(ctx, +1 / -1 / 0)`). Everything held is let go on a mode switch (the controls still down stay out until let go), focus loss, the pointer being switched off, a host app change, and in gamepad mode a panel.
- **Sticks.** `stick:scroll` is the smooth wheel (`xr/core/xr_scroll.h`, both axes, `scroll_speed`); `stick:mouse` moves the host mouse; `wasd`, `arrows` and `dpad` are spread over the four directions where those are bound to nothing (`xrBindTableExpandSticks`); a direction bound to something of its own is taken out of the stick's scroll or pad stick.
- **Reloads.** The file is read again whenever a stream starts (the host app is named), so a change made in the launcher's editor takes effect at the next stream start, as `docs/BINDINGS.md` says. A grip the file names (even `pad:lb`/`pad:rb`, as the editor writes) is the file's: `pad_grip` only applies to a grip left at its default.
- **Not yet:** the touch controls (no OpenXR actions for them), an in-headset capture editor.

## The launcher's editor (PLAN 2.6)

`launcher/qml/XrBindingsView.qml` (the page) over `launcher/src/mlxrbindings.{h,cpp}` (`MlxrBindings`, a QML singleton). `launcher/build.sh` copies `xr/core/xr_bind.c`, `xr_bind.h`, `xr_gamepad.c`, `xr_gamepad.h` and `xr_shared.h` next to the launcher sources and `launcher/patches/0013-*.patch` builds them with qmake, so the launcher has no parser of its own: it calls `xrBindLoadFile` (which writes the default file on the first open), `xrBindParse` (rules with their line numbers, errors), `xrBindActionParse`/`Format`, `xrBindResolve` (what a control does for an app and the layer it came from), `xrBindSourceFind`/`KeyName`, and `xrBindSerialize` (for the tests).

Saving never calls `xrBindSerialize` (it drops comments). `MlxrBindings` edits the file text on the lines `XrBindRule.line` names: replace the value after `=` keeping any trailing comment, delete the line, or insert after the last line of the same section (`[pointer]`/`[gamepad]` by mode, `[app "Name"]` by app index), else append a new section block (`
[mode]
line`). The result is parsed again and written (`QSaveFile`) only if the rule is in it with the action asked for; otherwise the core's error for that line is returned and nothing is written. Lines are split on `
` and joined unchanged, so CRLF files stay CRLF.

Tests (`tests/launcher/run_tests.sh`): `MLXR_LAUNCHER_PAGE=bindings` opens the page, `MLXR_LAUNCHER_CLICK=` presses rows, kind and value buttons by `objectName` (`row_<control>`, `kind_<kind>`, `opt_<kind>_<value>`, `key_<name>`, `mod_<ctrl|shift|alt|win>`, `resetRow`, `tabApp`, `pc_<PC>`, `app_<App>`, `modeGamepad`), `MLXR_LAUNCHER_BIND_OPS='set|<app or ->|<pointer|gamepad>|<control or chord>|<action>~reset|...~resetall|...'` edits at start without the UI, and `MLXR_LAUNCHER_BIND_DUMP=1` logs the core's `xrBindSerialize` of the file after every load and save. At start the launcher also checks that every choice it offers is accepted by the core and every reserved chord refused (`MLXR_BINDINGS: choices ok`).

## Notes the core lane left for the integration (kept for reference)

- **Load** once at start (`xrBindLoadFile(&set, NULL, &errs)`), log every `errs.errors[i].text` with the file name, and resolve a table per mode when the session starts, when the host app is known (`Settings.app`) and on a mode switch.
- **Inputs:** the two `PadHand`s after `padChordApply` (so the switch shortcut's parts are already held back), through `xrBindSourcesDown` with last frame's bits. For the triggers use `ctx->triggerDown` with `triggerSwallowed` taken out instead of the plain hysteresis, so press stabilisation keeps working. Take the grips out while the ray is on our UI. Touch bits come in once the touch actions are bound.
- **Pointer mode:** `xrBindMouseMask` replaces `pointerButtonsWanted` (same bits for the first three, plus 8 and 16); widen `dispatchInput`'s `MAP[3]` to `xrBindHostButton` over five bits. `MLXR_STICK_RIGHT_CLICK=0` should keep working: while `from[...STICK_CLICK]` is `XR_BIND_FROM_DEFAULT`, honour the env var as today.
- **Gamepad mode:** `xrBindPadButtons` replaces `padMapGrips`' buttons; triggers bound to `pad:lt`/`pad:rt` keep their analogue value, sticks bound to `stick:pad_left`/`pad_right` go through `padStick`. While a grip's `from` is `XR_BIND_FROM_DEFAULT`, apply `pad_grip` from `config.ini` as today. Keep running `padGuideStep` for the default View+Menu chord (the table's chord says Guide, `padGuideStep` adds the quarter second hold) and `padExitUpdate` on the result.
- **Keys, wheel, app commands** are edges: diff `xrBindHeld` against last frame's list (it may list an action twice when two controls share it). Keys need a new `XrAppHooks` entry to `sendKey`'s path; everything held is let go on a mode switch, focus loss, panel up or stream end, as `padRest` does.
- **Stick directions** bound to something take that direction from the whole stick's `stick:` action.
