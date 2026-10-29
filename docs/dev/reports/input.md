# input report (lane I, branch lane/gamepad)

Gamepad mode's output to the host and the host's rumble back to the Frame controllers. Library `mlxr_input_core` (pure, tests link it) and `mlxr_input` (adds `padOutUseLimelight`, links moonlight-common-c; the frame loop links this one). Include dirs: `input/`, xr/core, moonlight-common-c/src.

## Ported from
- `XrRenderer.dispatchPad()` + `ControllerHandler.attachXrPad/reportXrPad/detachXrPad` (arrival, flip face buttons, send on change, last packet at rest with the mask that no longer has the pad) -> `pad_out.c`.
- `XrRenderer.setRumble/rumbleLock/rumbleCount/rumbleWord/rumbleHanded` + `XrPad.rumbleWord` + `ControllerHandler.handleRumble` (controller number filter) -> `rumble.c`. The pulse logic itself (100 ms pulses re-armed every 50 ms, `fresh` flag) was already ported as `PadRumble`/`padRumbleStep` in xr/core/xr_gamepad.c and is reused unchanged.
- moonlight-qt gamepad.cpp: it sends on every SDL event, no dedup, no keep-alive. moonlight-common-c's gamepad channel is ENet RELIABLE and it batches same-button packets itself, so no keep-alive is needed; one exists in `PadOut` (`padOutSetKeepAlive`, default off) if a host ever wants it.

## API

### pad_out.h (frame loop only, not thread safe)
```c
PadOut pad; PadOutSink sink;
padOutUseLimelight(&sink);          // or a test sink: arrival/state function pointers + user
padOutInit(&pad, &sink);
padOutSetNumber(&pad, 0, 0);        // controller number on the host, bits of other pads (none natively)
padOutSetFlipFace(&pad, prefs.flipFaceButtons);   // may change any frame
int padOutFrame(PadOut*, int plugged, const PadState* pad, int64_t nowNs);
int padOutRest(PadOut*, int64_t nowNs);            // focus lost: let go of everything, stay plugged in
int padOutUnplug(PadOut*);                         // pad out, always sends the neutral packet
int padOutMask(const PadOut*);
```
Pure helpers (tested): `padOutButtonFlags`, `padOutFlipFaceFlags`, `padOutTrigger`, `padOutAxis`, `padOutConvert`.

Behaviour of `padOutFrame(plugged, pad)`:
- plugged 0: if it was plugged, sends a neutral packet with the mask without this pad (this is how the host hears it unplugged) and returns; otherwise nothing.
- plugged 1, first call after unplugged: `LiSendControllerArrivalEvent(n, mask, LI_CTYPE_XBOX, flags(PAD_BUTTONS)=0xf3ff, LI_CCAP_ANALOG_TRIGGERS|LI_CCAP_RUMBLE)` once, then the first state whatever it is.
- afterwards `LiSendMultiControllerEvent(n, mask, ...)` only when the converted packet differs from the last sent (two PadStates that convert equal are one state).
- Buttons: explicit table PAD_* -> Limelight flags, bits outside PAD_BUTTONS dropped. Triggers 0..255, sticks held to int16, y up positive (as xr_gamepad produces them: no inversion). Flip face buttons swaps A/B and X/Y in the flags, as upstream does after padMap, never before (so the Frame layout's X/Y etc. stay right).
- Arrival is sent again on every plug-in after an unplug (mode toggled off then on), as upstream's attachXrPad does; the host dropped the pad on the unplug.

### rumble.h
```c
Rumble* r = rumbleGlobal();                   // the one instance the C callbacks reach
rumbleSetController(r, n);                    // pad number on the host (0)
rumbleSetFrequency(r, 0, 0);                  // Hz per hand, default XR_FREQUENCY_UNSPECIFIED
// ConnListenerRumble = rumbleHostCallback; ConnListenerRumbleTriggers = rumbleTriggersCallback (ignored)
void rumbleOnHost(r, controller, low, high);  // any thread; low motor -> left hand, high -> right
void rumbleStop(r);                           // any thread: stream ending, next poll stops both hands
void rumbleReset(r);                          // frame loop: pad came out, drop what was asked
void rumblePoll(r, const int live[2], int64_t nowNs, RumbleRequest out[2]);   // frame loop, each frame
int  rumbleVibration(const RumbleRequest*, XrHapticVibration*);               // 1 and filled for APPLY
```
`RumbleRequest{action, amplitude 0..1, frequency, durationNs}`; `action` is `PAD_RUMBLE_KEEP` (do nothing), `PAD_RUMBLE_APPLY` (xrApplyHapticFeedback with the vibration) or `PAD_RUMBLE_STOP` (xrStopHapticFeedback). Hand index 0 = left (low motor), 1 = right (high motor), same as `HAND_LEFT/RIGHT`. Handshake: each host call increments `count` under a mutex; the poll takes a word when `count != handed`, so the same values again still arm a pulse at once. A rumble the host never ended stops when `live[h]` goes 0 (mode off, focus lost, no controller in that hand), on `rumbleStop`, or never starts for another controller number.

## Calls the M4 xr_input port must make
Setup (stream start, before `LiStartConnection`):
1. `padOutUseLimelight(&sink); padOutInit(&padOut, &sink); padOutSetFlipFace(...)`; `rumbleSetController(rumbleGlobal(), 0)` (and the same number to `padOutSetNumber`).
2. In the connection listener table: `.rumble = rumbleHostCallback, .rumbleTriggers = rumbleTriggersCallback` (do not advertise LI_CCAP_TRIGGER_RUMBLE; padOut does not).

Each input frame (replaces `settlePad`'s Java hand-off and `updateRumble`'s `padRumbleStep`):
3. After `settlePad` computes the final `PadState s` (at rest when not live, held-back controls removed) and `plugged = padPlugged(ctx)` (mode on and a controller in a hand): `padOutFrame(&padOut, plugged, &s, nowNs());`. This covers the mode toggle (plugged goes 0 -> unplug packet; 1 -> arrival + state) and focus loss (`restPad` gives s at rest while plugged -> one neutral packet; `padOutRest` is the same call if the port prefers it).
4. Haptics, in place of `padRumbleStep`: `int live[2] = { live && ctx->padAttached && profileKind[h]==PROFILE_CONTROLLER ... }; RumbleRequest q[2]; rumblePoll(rumbleGlobal(), live, nowNs(), q);` then per hand: if `q[h].action == PAD_RUMBLE_APPLY` { `XrHapticVibration v; rumbleVibration(&q[h], &v); xrApplyHapticFeedback(session, &info(h), (XrHapticBaseHeader*)&v);` } else if STOP `xrStopHapticFeedback`. Skip when the haptic action is not bound, as upstream.
5. When the pad is out (`!padAttached`): `rumbleReset(rumbleGlobal())` (upstream's `padRumbleReset` branch).

Teardown (stream stop): call `padOutUnplug(&padOut)` BEFORE `LiStopConnection` (the sends fail after it), and `rumbleStop(rumbleGlobal())` so the next poll stops the motors.

The xr_input `PadState`/`PadHand` side is untouched: padMap, padHoldBack, the toggle and chords stay in xr_input exactly as in upstream; the log lines for plug/unplug can key off `padOut.arrivals/states/unplugs`.

## Mapping table (PAD_ -> Limelight, all asserted in tests)
| PAD_ | value | Limelight | Frame source (via padMap) |
|---|---|---|---|
| UP | 0x0001 | UP_FLAG | left d-pad up (`dpadUp`) |
| DOWN | 0x0002 | DOWN_FLAG | left d-pad down (`lower` on Frame left) |
| LEFT | 0x0004 | LEFT_FLAG | left d-pad left (`dpadLeft`) |
| RIGHT | 0x0008 | RIGHT_FLAG | left d-pad right (`upper` on Frame left) |
| START | 0x0010 | PLAY_FLAG | right menu (`start`); Touch left menu |
| BACK | 0x0020 | BACK_FLAG | left view (`menu` on Frame left) |
| LS_CLICK | 0x0040 | LS_CLK_FLAG | left stick click |
| RS_CLICK | 0x0080 | RS_CLK_FLAG | right stick click |
| LB | 0x0100 | LB_FLAG | left bumper, left grip |
| RB | 0x0200 | RB_FLAG | right bumper, right grip |
| A | 0x1000 | A_FLAG | right A (`lower`) |
| B | 0x2000 | B_FLAG | right B (`upper`) |
| X | 0x4000 | X_FLAG | right X (`faceX`); Touch left X |
| Y | 0x8000 | Y_FLAG | right Y (`faceY`); Touch left Y |
Triggers: padTrigger -> 0..255 (13 percent dead zone) -> unsigned char. Sticks: padStick -> -32766..32766 -> short, y up positive (dead zone 7 percent, or the launcher slider when above 0). Guide (PAD_GUIDE 0x0400 -> SPECIAL_FLAG, advertised in the arrival) only from the left View + right Menu hold (`PadGuide`, xr_gamepad.c). The `pad_grip` setting makes the grips bumpers (default), nothing, or the stick clicks. No paddles.

## Tests (ctest names input_*, -Werror, clang 19)
`env -u LD_LIBRARY_PATH ctest --test-dir build-input -R input --output-on-failure`: 4/4 pass.
- input_pad_out: 4320 checks. Every PAD_ bit and all 14*14 pairs, PAD_BUTTONS = 0xf3ff, bits outside dropped; STEAM_FRAME.md table row by row through real `padMap` with Frame-profile `PadHand`s (face buttons, Start, d-pad four ways, Back, bumpers, grips, stick clicks, Touch hands, everything at once); flip (single, combined, involution, setting changed between frames); trigger/stick scaling and clamping from floats and from out-of-range ints; dedup per field; arrival once with type/caps/buttons/mask; unplug and focus-loss neutral packets; re-plug re-announces; controller number and mask (bit 15); keep-alive.
- input_rumble (1354) and input_rumble_tsan (same binary built with -fsanitize=thread, halt_on_error): low/high split, pulse and re-arm cadence (19 re-arms in the following second), same values re-arm, stop on zero, no stop when nothing ran, live=0 stops a hand, rumbleStop/rumbleReset, controller number filter, XrHapticVibration fill and clamping, global callbacks, 3 writer threads hammering `rumbleOnHost` (+ `rumbleStop`) against polling and a torn-word check, then the global instance the same. TSan clean.
- input_limelight (5): `padOutUseLimelight` against the real moonlight-common-c with no connection: the sends are harmless.

## Open questions
- Arrival is re-sent each time the pad is plugged back in (upstream behaviour). The task said "once when gamepad mode first turns on"; if the host should hear exactly one arrival per session, drop `plugIn()`'s arrival on re-plug (it would then also need a state-only re-attach, which Sunshine may not accept after the removal packet).
- Controller number: natively there are no other pads, so 0. If the app ever runs real pads too, call `padOutSetNumber` with the first free number and the same number to `rumbleSetController` (upstream's `XrPad.numberFor`).
- Frequency: upstream sends XR_FREQUENCY_UNSPECIFIED. If the Frame's linear haptics feel wrong for the low motor, `rumbleSetFrequency(r, lowHz, highHz)` is the knob; untested on hardware.
- No trigger rumble on the Frame, so `rumbleTriggers` is ignored and not advertised.
- Nothing here talks to a real host or the controllers' haptics; both were tested only through the hooks.

# Physical gamepads (lane/gamepads, 2026-10-10)

Normal gamepads connected to the Frame (Bluetooth or USB) reach the host as Moonlight gamepads while the VR client is focused and streaming, the way the physical keyboard does. Native addition; behaviour follows moonlight-qt's `app/streaming/input/gamepad.cpp`.

## Files
| File | Role |
|---|---|
| `input/gamepads.[ch]` | pure core (library `mlxr_input_core`): slots, controller numbers, active mask, arrival / state / removal, gating, disconnect chord, rumble routing, Steam Input copy handling. No SDL. The app-wide instance and `gamepadsHostRumble*` are at the end |
| `input/gamepads_sdl.c` | the SDL2 GameController source (library `mlxr_input_sdl`, linked by `moonlight-xr`): own thread, hotplug, mappings, reading a pad's state, rumble, player LEDs |
| `xr/xr_pad_bridge.c` | three small hooks: `gamepadsStreamStart/Stop` (with the stream) and the Frame pad claims its controller number from the same pool |
| `stream/session.c` | the connection's rumble callbacks are `gamepadsHostRumble(Triggers)`: a physical pad takes its own number, any other goes on to `input/rumble` (gamepad mode) |
| `app/main.c` | `gamepadsStart(padDisconnectHook)`, `gamepadsSetActive(...)` next to `evkbd_set_active` (same gate: streaming, focused, `stream_running()`), `gamepadsStop` |
| `tests/input/test_gamepads.c` | ctest `input_gamepads` and `input_gamepads_tsan` |

## Behaviour
- SDL2 (system `libSDL2`, pacman-owned, passes `tools/ldd-audit.sh`) with `SDL_INIT_GAMECONTROLLER`, no window: background events allowed, SDL's signal handlers off (SIGTERM must still end the app), `USE_BUTTON_LABELS=0` (positional A/B/X/Y as moonlight-qt), PS4/PS5 HIDAPI rumble on. Mappings: SDL's built-in database, `SDL_GAMECONTROLLERCONFIG`, then `$MLXR_GAMECONTROLLERDB`, the flat launcher's `~/.cache/Moonlight XR Native/Moonlight XR Native/gamecontrollerdb.txt` (436 mappings today) and `~/.config/moonlight-xr/gamecontrollerdb.txt`. Pads without a mapping are not game controllers and are ignored (the Frame's `als31300` hall sensor shows up as a joystick that way).
- Arrival: `LiSendControllerArrivalEvent(number, mask, type, buttons, caps)` as moonlight-qt: type from SDL (Xbox, PS, Nintendo, Steam), the buttons SDL says the mapping has, caps analogue triggers / rumble / trigger rumble. Touchpad, motion, LED, battery are not advertised (not done). Each arrival logs one line with name, VID/PID, GUID, type, caps and the mapping string; removals one line.
- State: `LiSendMultiControllerEvent` on change only (axis bursts are one read), moonlight-qt's button map (SPECIAL, MISC, paddles and touchpad included), stick Y inverted, triggers 0..255. Removal is a last packet at rest with the mask that no longer has the pad.
- Gating, as the keyboard: nothing is sent unless streaming and focused. The first focus announces every pad and sends its state; focus lost puts every pad at rest on the host and stops its rumble (the pad stays known); the stream ending takes each pad off the host, the next stream starts clean. A pad plugged in while unfocused is announced at the next focus. The headset UI (cog, keyboard) does not block pads, as it does not block the keyboard. There is no exclusive grab (SDL's own handle would lose its events with an `EVIOCGRAB`), so the pad also reaches SteamOS (Steam Input) if it listens.
- Controller numbers: lowest free, 0..15, shared with gamepad mode. The Frame pad claims the lowest number the physical pads do not hold when it plugs in and gives it back when it unplugs; its packets carry the mask of the announced physical pads (`padOutSetNumber(..., gamepadsAnnouncedMask())` each frame), and theirs carry its bit, so the host keeps all of them. `rumbleSetController` follows the Frame pad's number.
- Rumble: `ConnListenerRumble` for a physical pad's number goes to `SDL_GameControllerRumble(low, high, 30 s)`, trigger rumble to `SDL_GameControllerRumbleTriggers`; a stop is always passed, a start only while focused.
- Disconnect: exactly Start+Select+L1+R1 on one physical pad (moonlight-qt's test, `NO_GAMEPAD_QUIT=1` turns it off) puts the pad at rest on the host and calls `padDisconnectHook`, the same path as the Frame pad's chord (`launcher_disconnect`, the host's session stays). The pad stays at rest until the four are let go.
- Steam Input: Steam shows "Steam Virtual Gamepad" (28de:11ff) next to the real pad. It is not a second pad: SDL's copy joins the real pad that has none (either order), and the host sees the real pad while it is in use, else the copy while that is (Steam takes a pad over by silencing the real device; its echo of a real press is a little off and is ignored). Pads with the same serial, VID and PID are one pad. A virtual pad with no real pad to join is a pad of its own. With two real pads and two copies the pairing is by order, which may cross them. Verified with a uinput Xbox 360 pad on this Frame with Steam running (it did spawn a virtual copy). Controllers named "Frame" from Valve (28de) are skipped (SteamVR's own); `MLXR_GAMEPAD_IGNORE=<guid>,<guid>` skips others; `MLXR_GAMEPADS=0` turns the module off.
- Logging: arrivals / removals / chord / dedup in `moonlight.log`; one line per 5 s ("N packets in 5 s ...") only while pads send.
- Not done: the Frame's face-button flip setting is not applied to physical pads; DualSense touchpad, motion, LED and adaptive triggers; battery.

## Tests
`input_gamepads` (120 checks, also under ThreadSanitizer as `input_gamepads_tsan`): fake source and recording sink. Arrival then state, change-only sends, removal with the new mask, number reuse and the 16-pad limit, Frame pad claim / release against physical pads and masks, serial dedup, Steam copy (both orders, noise, real wins while used, copy carries a silenced pad, either one going), gating (unfocused, refocus, late arrival, stream end and restart), the chord (extra button, hold, re-arm, off switch), rumble routing and the Frame pad's number left alone, the app-wide instance, and 5 threads (state, rumble, focus, claims, hotplug) at once. Also checked with a throwaway uinput harness against the real SDL source on this Frame (no stream): arrival, state, unfocus and refocus, chord hook, Steam copy merge, unplug.

## Owner test
1. Pair a pad over Bluetooth in SteamOS (Xbox, DualSense, 8BitDo or Switch Pro). Start a stream in VR as usual.
2. On the PC open a gamepad tester (https://hardwaretester.com/gamepad or Windows "Set up USB game controllers"). Press every button, move both sticks, squeeze both triggers: each shows. The Frame controllers in gamepad mode (cog toggle) still work at the same time and show as a second pad.
3. Rumble: use the tester's vibration button or a game with rumble.
4. Take the headset UI up (cog) and look away: pad input keeps going (or stops when the VR client loses focus, as the keyboard).
5. Turn the pad off: it disappears on the PC; turn it on: it comes back.
6. Hold Start+Select+L1+R1 on the physical pad: the VR client leaves the stream and the host's session stays (flat UI shows Resume), same as the Frame pad's chord.
7. `moonlight.log` has one `gamepad #N arrived: ...` line per pad (name, GUID, mapping). If a pad does nothing, send that line.

# Physical mice (lane/mouse, 2026-10-10)

A USB or Bluetooth mouse acts as the host's mouse while the VR client is focused and streaming. Native addition; behaviour follows moonlight-qt's `app/streaming/input/mouse.cpp`.

## Files
| File | Role |
|---|---|
| `input/evmouse.[ch]` | library `mlxr_input_core`: evdev reader thread (hotplug scan every 2 s, `EVIOCGRAB` while active, released otherwise), forwarding through a sink, and the pointer filter |
| `stream/session.c` | `stream_hscroll` (`LiSendHighResHScrollEvent`) next to the existing `stream_mouse_move/button/scroll` |
| `app/main.c` | sink hooks, `evmouse_set_active` next to `evkbd_set_active` (same gate: streaming, focused, `stream_running()`; the headset UI does not block it, as with the keyboard), `pointerHook` asks `evmouse_abs_filter` first |
| `tests/input/test_evmouse.c` | ctest `input_evmouse` (pipes as fake evdev, recording sink, fake grab) |

## Behaviour
- Device: EV_REL with REL_X and REL_Y and BTN_LEFT (touchpads and tablets are ignored; names starting "Steam" and `MLXR_MOUSE_IGNORE=<substring>` are skipped). Read-only on this Frame: a Bluetooth keyboard's "Mouse" interface (event7, crw-rw---- root:input, user is in `input`) qualifies, so no sudo is needed.
- Motion: REL_X/REL_Y summed per SYN_REPORT, one `LiSendMouseMoveEvent` per packet (split above a short). `MLXR_MOUSE_SCALE=<f>` scales it, the fractional part is carried between packets. SYN_DROPPED discards the packet in flight.
- Buttons: left, right, middle, BTN_SIDE/BTN_BACK = X1, BTN_EXTRA/BTN_FORWARD = X2 (`LiSendMouseButtonEvent`). A press made while not forwarding is never sent, and buttons the host has down are released when forwarding stops.
- Wheel: REL_WHEEL_HI_RES and REL_HWHEEL_HI_RES (120 per notch) to `LiSendHighResScrollEvent` / `LiSendHighResHScrollEvent`; a mouse without hi-res sends REL_WHEEL/REL_HWHEEL times 120. With hi-res the legacy notch events are ignored (the kernel sends both).
- Grab: `EVIOCGRAB` while active, as the keyboard; a failed grab is logged and the mouse is forwarded anyway. `MLXR_MOUSE=0` turns the module off.
- Controller pointer: the ray sends absolute positions, the mouse relative ones. After any forwarded mouse activity the ray's absolute updates are dropped for 1 s, and then until the aim has moved 3% of the video width from where it was at the last mouse use ("the controller pointer is parked" / "its pointer resumes" in `moonlight.log`). `MLXR_MOUSE_PARK=0` switches this off, `MLXR_MOUSE_PARK_MS` and `MLXR_MOUSE_PARK_DIST` tune it. The pointer's smoothing and click hold are untouched; the dot in the headset still follows the controller, only the host cursor is left to the mouse.

## Owner test
1. Pair the mouse in SteamOS Bluetooth settings. Start a stream in VR as usual (`moonlight.log` says `mouse found: <name>` and `grabbed for the host`).
2. Move, click left/right/middle (and side buttons), scroll up/down and sideways (tilt wheel): the host cursor follows, clicks and scrolls land.
3. Pick up a controller and aim: after a second of no mouse use and a visible aim movement the host cursor goes to the ray again; moving the mouse takes it back.
4. Take the headset UI out of focus or end the stream: the mouse is released to SteamOS. If nothing happens, send the `mouse` lines of `moonlight.log`.
