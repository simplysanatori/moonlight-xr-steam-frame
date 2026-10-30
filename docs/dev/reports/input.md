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
| `input/pad_out_ll.c` | `gpUseLimelightExtras`: motion, battery and touch sends to moonlight-common-c (checked with no connection by `input_limelight`) |

## Behaviour
- SDL2 (system `libSDL2`, pacman-owned, passes `tools/ldd-audit.sh`) with `SDL_INIT_GAMECONTROLLER`, no window: background events allowed, SDL's signal handlers off (SIGTERM must still end the app), `USE_BUTTON_LABELS=0` (positional A/B/X/Y as moonlight-qt), PS4/PS5 HIDAPI rumble on. Mappings: SDL's built-in database, `SDL_GAMECONTROLLERCONFIG`, then `$MLXR_GAMECONTROLLERDB`, the flat launcher's `~/.cache/Moonlight XR Native/Moonlight XR Native/gamecontrollerdb.txt` (436 mappings today) and `~/.config/moonlight-xr/gamecontrollerdb.txt`. Pads without a mapping are not game controllers and are ignored (the Frame's `als31300` hall sensor shows up as a joystick that way).
- Arrival: `LiSendControllerArrivalEvent(number, mask, type, buttons, caps)` as moonlight-qt: type from SDL (Xbox, PS, Nintendo, Steam), the buttons SDL says the mapping has, caps analogue triggers / rumble / trigger rumble, plus (Wave 2b, 2b.2) touchpad, accelerometer, gyroscope, battery and RGB LED when SDL says the device has them (`gpCapsFor`, moonlight-qt's rule; battery is always advertised on SDL 2.24+). The Frame pad (`pad_out.c`, `PADOUT_CAPS`) never advertises any of these. Each arrival logs one line with name, VID/PID, GUID, type, caps and the mapping string; removals one line.
- State: `LiSendMultiControllerEvent` on change only (axis bursts are one read), moonlight-qt's button map (SPECIAL, MISC, paddles and touchpad included), stick Y inverted, triggers 0..255. Removal is a last packet at rest with the mask that no longer has the pad.
- Gating, as the keyboard: nothing is sent unless streaming and focused. The first focus announces every pad and sends its state; focus lost puts every pad at rest on the host and stops its rumble (the pad stays known); the stream ending takes each pad off the host, the next stream starts clean. A pad plugged in while unfocused is announced at the next focus. The headset UI (cog, keyboard) does not block pads, as it does not block the keyboard. There is no exclusive grab (SDL's own handle would lose its events with an `EVIOCGRAB`), so the pad also reaches SteamOS (Steam Input) if it listens.
- Controller numbers: lowest free, 0..15, shared with gamepad mode. The Frame pad claims the lowest number the physical pads do not hold when it plugs in and gives it back when it unplugs; its packets carry the mask of the announced physical pads (`padOutSetNumber(..., gamepadsAnnouncedMask())` each frame), and theirs carry its bit, so the host keeps all of them. `rumbleSetController` follows the Frame pad's number.
- Rumble: `ConnListenerRumble` for a physical pad's number goes to `SDL_GameControllerRumble(low, high, 30 s)`, trigger rumble to `SDL_GameControllerRumbleTriggers`; a stop is always passed, a start only while focused.
- Motion (2b.2, ports moonlight-qt `gamepad.cpp` sensor path): the host asks per pad and sensor with `ConnListenerSetMotionEventState` (`cl.setMotionEventState = gamepadsHostMotionState` in `stream/session.c`) and the rate it wants; `gpMotionState` accepts it only for a physical pad whose caps advertise that sensor (a pad without sensors, and the Frame pad's number, ignore it; the call returns 0 for a number that is not a physical pad's) and calls the `setSensor` hook (`SDL_GameControllerSetSensorEnabled`, SDL has no rate) only while the pad is on the host: focus lost, stream end and unplug switch the sensor off, focus back re-enables it at the stored rate, and a new stream starts with no requests (the host asks again). `SDL_CONTROLLERSENSORUPDATE` goes to `gpMotion`, which keeps at most one sample per `1000/rate` ms of SDL's event timestamp (3 ms jitter on a 250 Hz DualSense costs a sample now and then, as moonlight-qt), drops a sample equal to the last one sent, converts the gyroscope rad/s to deg/s (x 57.2957795) and leaves the accelerometer in m/s^2, and sends `LiSendControllerMotionEvent` on the pad's own number. Unlike moonlight-qt the first sample after enabling is not throttled and a rate above 1000 Hz is every sample (qt's period of 0 would mean off). Samples come only from the real pad, never from its Steam Input copy, which has no sensors. Hosts: Sunshine and Apollo (the protocol extension is Sunshine-only; `LiSendControllerMotionEvent` returns `LI_ERR_UNSUPPORTED` elsewhere); Apollo emulates a DualShock 4 for a `LI_CTYPE_PS` pad with gyro, which is what the PC's Steam Input then sees.
- Battery (2b.2): `SDL_JoystickCurrentPowerLevel` at arrival and `SDL_JOYBATTERYUPDATED` (SDL 2.24) map through `gpBatteryFromPowerLevel` to `LiSendControllerBatteryEvent` (moonlight-qt's table: wired = charging, empty/low/medium/full = discharging at 5/20/50/90 %), sent after the arrival and again only when it changes.
- Touchpad (2b.2): `SDL_CONTROLLERTOUCHPAD*` for touchpad 0 goes to `LiSendControllerTouchEvent` (down/move/up, SDL's 0..1 coordinates and pressure, finger id as pointer id), only for a pad advertising `LI_CCAP_TOUCHPAD` (a DualSense). Fingers the host has down get `LI_TOUCH_EVENT_CANCEL_ALL` when focus is lost, the stream ends or the pad goes. The touchpad click is the existing `TOUCHPAD_FLAG` button. moonlight-qt's Select+PS touchpad-click emulation is not ported.
- LED (2b.2): `ConnListenerSetControllerLED` for a physical pad with `LI_CCAP_RGB_LED` goes to `SDL_GameControllerSetLED`, only while the pad is on the host. Adaptive triggers are not forwarded: moonlight-qt 6.1.0, the reference, does not either (`setAdaptiveTriggers` is not wired).
- Disconnect: exactly Start+Select+L1+R1 on one physical pad (moonlight-qt's test, `NO_GAMEPAD_QUIT=1` turns it off) puts the pad at rest on the host and calls `padDisconnectHook`, the same path as the Frame pad's chord (`launcher_disconnect`, the host's session stays). The pad stays at rest until the four are let go.
- Steam Input: Steam shows "Steam Virtual Gamepad" (28de:11ff) next to the real pad. It is not a second pad: SDL's copy joins the real pad that has none (either order), and the host sees the real pad while it is in use, else the copy while that is (Steam takes a pad over by silencing the real device; its echo of a real press is a little off and is ignored). Pads with the same serial, VID and PID are one pad. A virtual pad with no real pad to join is a pad of its own. With two real pads and two copies the pairing is by order, which may cross them. Verified with a uinput Xbox 360 pad on this Frame with Steam running (it did spawn a virtual copy). Controllers named "Frame" from Valve (28de) are skipped (SteamVR's own); `MLXR_GAMEPAD_IGNORE=<guid>,<guid>` skips others; `MLXR_GAMEPADS=0` turns the module off.
- Logging: arrivals / removals / chord / dedup in `moonlight.log`; one line per 5 s ("N packets in 5 s ...") only while pads send.
- Not done: DualSense adaptive triggers; Select+PS touchpad-click emulation; a second touchpad (`LI_CCAP_DUAL_TOUCHPAD`, Steam Controller).

## Tests
`input_gamepads` (120 checks, also under ThreadSanitizer as `input_gamepads_tsan`): fake source and recording sink. Arrival then state, change-only sends, removal with the new mask, number reuse and the 16-pad limit, Frame pad claim / release against physical pads and masks, serial dedup, Steam copy (both orders, noise, real wins while used, copy carries a silenced pad, either one going), gating (unfocused, refocus, late arrival, stream end and restart), the chord (extra button, hold, re-arm, off switch), rumble routing and the Frame pad's number left alone, the app-wide instance, and 5 threads (state, rumble, focus, claims, hotplug) at once. Also checked with a throwaway uinput harness against the real SDL source on this Frame (no stream): arrival, state, unfocus and refocus, chord hook, Steam copy merge, unplug.

Motion, battery, touchpad and LED (same test, `testMotion*`, `testBatteryTouchLed`): a DualSense-like pad advertises `ACCEL|GYRO` (and touchpad, battery, LED) and an Xbox-like one and the Frame pad's arrival word (`PADOUT_CAPS`) do not; a host request enables the sensor through the hook at rate N only once the pad is on the host; samples become motion events on the pad's own number with gyro in deg/s and accel unchanged; the 1000/N ms throttle, repeat suppression, a 4000 Hz and a 3 Hz rate and a wrapping clock; stop switches it off; a pad without sensors, the Frame pad's number and an unknown number take no sensor call; focus loss, stream end and unplug switch sensors off, a new stream starts with no requests; Steam copy samples are ignored; battery sent at arrival and on change only; touch events only for a pad with a touchpad and cancelled on focus loss; LED only for a pad with one and only while on the host. A worker for these also runs in the ThreadSanitizer build.

## Owner test
1. Pair a pad over Bluetooth in SteamOS (Xbox, DualSense, 8BitDo or Switch Pro). Start a stream in VR as usual.
2. On the PC open a gamepad tester (https://hardwaretester.com/gamepad or Windows "Set up USB game controllers"). Press every button, move both sticks, squeeze both triggers: each shows. The Frame controllers in gamepad mode (cog toggle) still work at the same time and show as a second pad.
3. Rumble: use the tester's vibration button or a game with rumble.
4. Take the headset UI up (cog) and look away: pad input keeps going (or stops when the VR client loses focus, as the keyboard).
5. Turn the pad off: it disappears on the PC; turn it on: it comes back.
6. Hold Start+Select+L1+R1 on the physical pad: the VR client leaves the stream and the host's session stays (flat UI shows Resume), same as the Frame pad's chord.
7. `moonlight.log` has one `gamepad #N arrived: ...` line per pad (name, GUID, mapping, caps). If a pad does nothing, send that line.
8. Gyro (DualSense over Bluetooth, Apollo or Sunshine on the PC): the arrival line has caps with 0x30 set (accelerometer and gyroscope, 0xff for a DualSense). Start a game or Steam's controller test on the PC, enable gyro there; `moonlight.log` has `the host asks for the gyroscope at N Hz`. Tilting the pad moves the PC's gyro; the PC must see a PlayStation pad (Apollo's DS4 emulation with ViGEmBus). Put the headset UI up or look away: motion keeps going while the VR client is focused and stops with focus. Pad numbers in the log tell which pad it is; the Frame controllers in gamepad mode never ask for or send motion.

# Mouse emulation, pad arbitration, flip and glyphs (lane wave2b-padmouse, 2b.4, 2b.5, 2b.7)

## Files
| File | What |
| --- | --- |
| `xr/core/xr_padmouse.[ch]` | Pure core (library `mlxr_xrcore`, test `xrcore_padmouse`): the chord clock, the speed curve, the buttons / arrows / wheel / pointer while a pad is a mouse, the neutral verdict, release-all, the toast words. No SDL, no Limelight, no clock of its own. The PAD_ bits are numerically Moonlight's button flags, so a physical packet goes in as it is (checked in `input_gamepads`) |
| `input/gamepads.[ch]` | Per-slot `PadMouse`, `gpTick`/`gpWantsTick`, the arbitration state and the glyph functions. `gpSetClock` lets a test own the clock |
| `input/gamepads_sdl.c` | The SDL loop waits `PADMOUSE_TICK_MS` (8) instead of 250 while a pad is a mouse or a chord is being held (a stick held still sends no event), and calls `gamepadsTick` |
| `xr/xr_pad_bridge.c` | `xrPadBridgeFrame` runs gamepad mode's `plugged` through `gamepadsFramePadAllowed` |
| `xr/xr_prefs.c`, `ui/settings.[ch]` | Settings `pad_mouse_chord`, `pad_mouse_scroll`, `pad_arbitration` (config.ini and the launcher mirror, no cog row, so no golden images); `flip_face_buttons` now also reaches the physical pads live |
| `app/main.c` | The sink: relative motion (also parks the controller ray like a real mouse), buttons, wheels, arrow keys, and the toast through `xr_app_notice` (Notices level honoured) |
| `launcher/` | Rows on the Gamepad Settings page (Gamepad as mouse, scroll stick, When a gamepad is connected) writing the same `[vr]` keys |

## 2b.4 behaviour
- Chord (`pad_mouse_chord`): `start_hold` = the pad's buttons are exactly Start for 1 s; `start_back` = exactly Start and Back for 1 s; `off`. Any other button joining spoils the hold and restarts it; sticks and triggers do not. Start goes to the game until the second is up (the game may use Start); when the mode flips, the buttons held then are kept off both the game and the mouse until let go. The same chord ends the mode.
- Mouse: the pointing stick goes through `60 t + 1740 t^2` px/s (t = travel past a 15 % dead zone, 1800 px/s at full tilt), sub-pixel remainders carried; a step after a pause counts as at most 50 ms. The scroll stick (right by default) scrolls on `1200 t^2` hi-res units/s (10 notches/s), whichever axis leans further. `pad_mouse_scroll` left makes the left stick scroll and the right one point; none leaves the right stick idle. A/B/X = left/right/middle, LB/RB = X1/X2 (Moonlight buttons 4/5), D-pad = arrow VKs, Y = `PadMouseSink.keyboard` (wired by lane wave2-bind-wire: the frame loop toggles the keyboard next frame). Face buttons are the physical positions (before the flip setting).
- The slot sends rest to the game while a mouse. The disconnect chord (Start+Select+L1+R1) is tested on the raw state and still works.
- Release-all: toggle, focus loss, stream end, pad removal (`padMouseRelease`: buttons and keys up, mode off, quietly). Focus loss and stream end end the mode (it is not remembered) and raise no toast.
- Toast: `padMouseNoticeText` through `xr_app_notice` (a TOAST_TEXT notice, which `noticeAllowed` drops at Notices = Off and keeps at Important).
- Hooks run on the gamepad thread under the pad lock. The keyboard hook must only set a flag for the frame loop.

## 2b.5 behaviour and the owner lane's calls
- A physical input (button, trigger above 12, stick |x|+|y| above 8000 on a pad that is on the host) sets `frameRetired` when the setting is not `also_gamepad` and the Frame pad holds a number. The bridge then sees `plugged = 0`: neutral packet with the mask without it, unplug (`padOutFrame`), `gamepadsReleaseFramePad`. Gamepad mode off for a frame re-arms it; so does the stream starting or ending. `also_gamepad` ignores the flag.
- `gamepadsPhysicalActive()` is true for 20 s after the last physical input (cleared when the last pad goes or the stream stops). `gamepadsFrameControllersAsleep()` = setting `sleep` and that.
- One line for the xr_input.c owner, where `ctx->pointerAwake` is settled (after the `pointerSleepOn` block, about xr_input.c:2575): `if (gamepadsFrameControllersAsleep()) { ctx->pointerAwake = 0; }`. Y as the VR keyboard: `gamepadsSetPadMouseSink` takes a `PadMouseSink` whose `keyboard` hook the frame loop owner can fill (set an atomic flag there, toggle the keyboard next frame); Both done in lane wave2-bind-wire (`xr_app_toggle_keyboard`, `xrKeyboardToggle`).

## 2b.7
- Flip: `xrApplySettings` now calls `gamepadsSetFlipFaceButtons(s->flip_face_buttons)` (it was only set at stream start by `main.c`), so the in-headset reload and the launcher agree; the swap is applied to what the host gets (`gpApplyStickOptions`), as the launcher's Artemis checkbox already did.
- Glyphs (pure, tested): `gpGlyphSetForType(LI_CTYPE_*)` gives Xbox / PlayStation / Nintendo / Steam (unknown and the Frame pad are Xbox); `gpFaceLabel(set, hostButton, flipped)` is the name printed on the physical button that sends A/B/X/Y (positions fixed: PlayStation Cross/Circle/Square/Triangle, Nintendo B/A/Y/X, flip swaps A<->B and X<->Y); `gamepadsGlyphSet()` is the set of the physical pad used last, Xbox before any. The hint panels and the first-run card are another lane's: they call these.

## Tests
`xrcore_padmouse` (curve, chord timing and choices, neutral verdict, every button / key / wheel / pointer path, held-at-toggle, release-all, no-sink). `input_gamepads` additions: Moonlight constants vs the PAD_ numbers, a pad becoming a mouse and back on a fake clock (host sees rest, stick and buttons through the sink, disconnect chord, focus loss, stream end, pad gone, chord off / start_back), two pads independent, flip on a physical packet, arbitration (dead-zone nudge is not use, retire, re-arm, trigger and stick, also / pointer / sleep with the 20 s idle, no Frame pad to retire, unfocused input, stream restart), glyph tables and the pad used last. Also under TSan as `input_gamepads_tsan`.

## Owner test with a Bluetooth pad (headset and PC)
1. Pair a pad in SteamOS, start a stream, turn the Frame controllers to gamepad mode (the PC tester shows the Frame as a pad). Press a button on the Bluetooth pad: the Frame pad disappears from the tester and the Bluetooth pad is the only one (`moonlight.log`: "is in use: gamepad mode's pad ... is taken off the host"). Toggle gamepad mode off and on: the Frame pad is back until the pad is used again. Set `pad_arbitration = also_gamepad`: both stay.
2. Hold Start on the Bluetooth pad for a second: the toast says the pad is a mouse, the PC cursor follows the left stick (feel: slow start, fast sweep), the right stick scrolls, A/B/X click, LB/RB back/forward, the d-pad is arrows, Y does nothing yet. The tester shows the pad at rest. Hold Start again: the toast says it is a gamepad again and the sticks work in a game. Take the headset off while a button is held: nothing stays pressed on the PC. With Notices = Off no toast.
3. Set `flip_face_buttons = true`: A and B swap on the Bluetooth pad as well.

# mlxr-gamepad-probe (lane wave2b-padprobe, 2026-10-29)

`build/input/mlxr-gamepad-probe` (also `mlxr-gamepad-probe` beside the client after `install.sh`, in the release tarball, `--help` for the options) lists every SDL joystick and game controller the way the physical pad source sees it, and with `--watch` prints hotplug and per-event state lines for a certification pass: `docs/dev/GAMEPAD_CERT.md`.

| File | What |
| --- | --- |
| `input/gamepads.[ch]` | Classification moved here from `gamepads_sdl.c` so it has no SDL: `gpSteamControllerPid`/`PidName`, `gpControllerType(GpSdlKind, vid, pid)`, `gpClassify`, `gpIgnoreReason` (Frame controller rule, `MLXR_GAMEPAD_IGNORE`). Behaviour unchanged |
| `input/gamepads_sdl_info.c`, `gamepads_sdl.h` | `gpSdlDescribe`, `gpSdlReadPacket`, `gpSdlApplyHints`, `gpSdlLoadMappings`, SDL type names: what `gamepads_sdl.c` did inline, now shared with the probe (library `mlxr_input_sdlinfo`) |
| `input/padprobe.[ch]` | No SDL: `PadProbe` runs each device through `gpIgnoreReason` and `gpAdd` on a throwaway `Gamepads` (controller number, Steam Input copy merge, serial duplicates) and prints the block, the summary, watch lines |
| `input/gamepad_probe.c` | The SDL side and the command line |
| `tests/input/test_padprobe.c` | ctest `input_padprobe`: classification and text for a pad with its Steam Input copy, a Frame controller (skipped), a DualSense with sensors, the 2026 Steam Controller ids, duplicates, an unmapped joystick |

Steam Controller ids (`gpSteamControllerPid`): 28de:1101, 1102, 1105, 1106, 1142 (2015 controller and its dongle), 1201, 1202 (HEADCRAB), 1205 (Steam Deck built-in), 1302, 1303 (2026 controller, USB and Bluetooth LE), 1304, 1305 (the two pucks). This is moonlight-qt 6.2.0's `app/streaming/input/gamepad.cpp` list and SDL main's `src/joystick/controller_list.h` (Valve products); 28de:11ff is Steam's virtual pad and is not a Steam Controller.

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

# Lightgun pointer policy and the Frame pad's gyro (lane wave2-lightgun-gyro, PLAN 2.3 and 2.9)

## Files
| File | What |
| --- | --- |
| `xr/core/xr_lightgun.[ch]` | Pure (library `mlxr_xrcore`, test `xrcore_lightgun`): `lightgunPolicy` (setting `lightgun` = auto / on / off and the controller model drawn make a `LightgunPolicy`), `lightgunSleepOn`, `lightgunClickConfig`, `lightgunTriggerMayGrab`, the off-picture reload latch (`lightgunButtons`) and `lightgunRecoilDue` |
| `xr/core/xr_padgyro.[ch]` | Pure (test `xrcore_padgyro`): `padGyroFromVelocity` (OpenXR angular velocity in rad/s about the base space, to deg/s about the device's axes), `padGyroFromTurn` (the same from two orientations, for a runtime with no velocity), `padGyroGravity`, `padGyroGateOpen` (the ratchet) |
| `xr/xr_input.c` | `refreshLightgun` (once per input frame, before anything reads the policy), `sleepOn`, the grab test in `applyGrab`, the click config in `sendPointer`, reload and recoil in `updateButtons`, `recoilPulse`, `xrPadGyroFrame` (after `xrPadBridgeFrame` in `settlePad`), `xrSetLightgun`, `xrSetPadGyro` |
| `xr/xr_pad_bridge.[ch]` | `xrPadBridgeSetMotion`, `xrPadBridgeMotionRate`, `xrPadBridgeMotion`, `xrPadBridgeHostMotionState` (the host's request for the Frame pad's number) |
| `input/pad_out.[ch]` | `PadOut.motion` (the opt-in), `padOutSetMotion`, `padOutMotionState`, `padOutMotionRate`, `padOutMotion` (rate gate, repeat drop), the arrival words `PADOUT_TYPE_MOTION` / `PADOUT_CAPS_MOTION`, `PadOutSink.motion` |
| `input/gamepads.[ch]` | `gamepadsSetFramePadMotionHook`: `gamepadsHostMotionState` offers a request to the physical pads first and then to this hook |
| `ui/settings.[ch]`, `xr/xr_prefs.c`, `launcher/` | `[vr] lightgun`, `lightgun_reload`, `lightgun_recoil`, `pad_gyro`, `pad_gyro_ratchet` (config.ini, plus launcher rows for `lightgun` and `pad_gyro`; no cog rows, so no golden images) |

## 2.3 behaviour
- The policy is on when `lightgun = on`, or `auto` and the controller model drawn is the Gun (`controllerModelKind(modelOn, modelKind)`); `off` never. It is recomputed every input frame, so a change of model from the cog applies at once. Turning it on or off drops the press fix, the click hold and the reload latch.
- It never writes into the stored settings (`pointerSleepOn`, `clickSteady`, `pressCfg` stay the user's): `sleepOn(ctx)` answers for pointer sleep, `sendPointer` hands the press fix and click hold a local copy with everything off, and the grab test asks `lightgunTriggerMayGrab`.
- Grab by grip only: `applyGrab`'s `byTrigger` needs `lightgunTriggerMayGrab`, for the picture's bar, its corners and the keyboard's bar alike.
- Reload (`lightgun_reload`, default off): a left button that starts (rising edge of the bindings' left bit) with the aim on nothing of ours (`HOVER_NONE`, `HOVER_HALO` or `HOVER_BAR`) latches a right click for the whole pull, whatever the aim does meanwhile; the left button is dropped for that pull. A pull on the picture, a corner or any button of ours is not a reload.
- Recoil (`lightgun_recoil`, default on): one `xrApplyHapticFeedback` of 40 ms at amplitude 1.0, frequency unspecified, through the same vibration action as the host's rumble, on the hand whose trigger edge it was, when the left button comes down on the picture or a reload pull begins. Needs the vibration bound (`hapticBound`); the first pulse logs the runtime's answer.
- The dot is not touched: `Ray` decides, and with pointer sleep held off the dot is there whenever a controller points.

## 2.9 behaviour
- `pad_gyro = off` (default): nothing changes, the arrival is `PADOUT_TYPE`/`PADOUT_CAPS` (Xbox, analog triggers and rumble). `controller` or `head`: the next arrival of the Frame pad is `LI_CTYPE_PS` with `PADOUT_CAPS | LI_CCAP_GYRO | LI_CCAP_ACCEL`, so Apollo/Sunshine emulates a DualShock 4. Only the Frame pad's own slot; physical pads keep what SDL says (`gpCapsFor`). The setting is read at arrival, so it applies when gamepad mode next turns on.
- `setMotionEventState` for the Frame pad's number reaches `padOutMotionState` through the new hook; a request for a pad that did not announce motion is refused (the physical-pad path is unchanged). Requests are forgotten when the pad is unplugged.
- Each input frame `xrPadGyroFrame` returns at once unless gamepad mode has the pad on the host and the host asked for a type (no OpenXR call otherwise). It locates the right controller's aim space (the left's if the right is not orientation-valid and tracked) or the view space in the local space with `XrSpaceVelocity` chained. The runtime's angular velocity is rotated into the device frame and converted to deg/s; a runtime that gives none gets the finite difference of two orientations (also when the hand changes: one frame of zero).
- Axes: the SDL game controller frame (x right, y up, z toward the player) is the OpenXR aim and view frame, so no swap is needed; moonlight-qt passes SDL's values straight (gyro in deg/s, accelerometer in m/s^2), and so do we. The accelerometer is gravity only: (0, 9.80665, 0) turned into the device frame. A still, level device reads (0, 9.81, 0).
- Ratchet: `pad_gyro_ratchet = stick_touch` sends gyro only while `ctx->stickTouchAction` (boolean, `sticktouch`, created with the Frame profile's actions) reads true on the right hand. It is bound to `/user/hand/right/input/thumbstick/touch` on the first rung of the Frame ladder only (`suggestFrameBindings(..., touch = 1)`: squeeze, vibration and touch), so a runtime that refuses the set costs the ratchet and nothing else; `stickTouchBound` records that a Frame profile took it. It counts only while `onFrame[HAND_RIGHT]`: a right hand on Touch emulation, the simple controller or hands reads as `always`, with a `LOGW` each time the fallback starts and a line when it ends. Closed, a panel up, or an unplaceable source sends a zero gyroscope (dropped once it repeats), so the host's DualShock 4 never keeps the last turn. The accelerometer is not gated.
- `padOutMotion` sends no faster than the rate the host asked for and drops a sample equal to the last one.
- A non-finite angular velocity from the runtime is zeroed per axis before it is turned into the device frame (`padGyroFromVelocity`); after the rotation a NaN on one axis would spread to the others through `0 * NaN`.

## The motion path, end to end
1. Setting: `xrSetPadGyro` (from `xr_prefs.c` at start and on change) stores the source and the ratchet and calls `xrPadBridgeSetMotion`, which sets `PadOut.motion`. Nothing is sent yet.
2. Arrival: when gamepad mode plugs the Frame pad in, `plugIn` sends `LiSendControllerArrivalEvent(number, mask, LI_CTYPE_PS, buttons, ANALOG_TRIGGERS | RUMBLE | GYRO | ACCEL)` and sets `motionAnnounced`. The number is the Frame pad's own slot (the one `xrPadBridgeFrame` already sends its state on); physical pads keep theirs.
3. The host's request: Sunshine/Apollo answers a PS arrival with `setMotionEventState(number, type, rateHz)` per sensor. The stream layer hands it to `gamepadsHostMotionState`, which offers it to the physical pads first (`gpMotionState` returns 1 if it owned the number) and then to the hook `xrPadBridgeHostMotionState`. That one takes it only for the Frame pad's number and only after a motion arrival (`padOutMotionState`), and logs `gamepad: the host asks for the gyroscope at N Hz`. A rate of 0 stops that sensor; unplugging forgets both.
4. Each input frame (`settlePad` -> `xrPadGyroFrame`): nothing is located unless a rate is set. The source is located with `XrSpaceVelocity` chained, converted (`padGyroFromVelocity`, or `padGyroFromTurn` without velocity), gated by the ratchet, and sent as `LiSendControllerMotionEvent(number, LI_MOTION_TYPE_GYRO, x, y, z)` in deg/s and `LI_MOTION_TYPE_ACCEL` gravity in m/s^2, each no faster than its asked rate.
5. On the PC the DualShock 4 that ViGEmBus makes carries the motion into its report, and Steam Input reads it as native gyro (gyro to mouse, gyro to joystick, its own ratchet on top if wanted).

## Tests
`xrcore_lightgun`, `xrcore_padgyro` (units, axes, device frame, limits including a bad axis on a turned device, finite difference, gravity, ratchet), `input_pad_out` (arrival with and without the opt-in, request handling, rate, repeat drop, unplug, replug), `input_gamepads` (the Frame pad's caps with the opt-in beside the physical pads', the request hook), `ui_settings` (the five keys), `m4_lightgun` (policy follows the model kind, pointer sleep held off through `xrPointerStep`, the trigger's grab, shots / reload / recoil through `updateButtons`, the gyro through the real pad bridge with a recording sink: controller, left fallback, head, no-velocity fallback, ratchet with the touch bound, with the right hand off the Frame profile and back, with the binding refused and with no action, rate, pointer mode).

## Owner test (headset, and a PC)
1. Lightgun: `controller_model = gun`. Aim at a game with a mouse cursor (an emulator's light gun mode): the ray stays up between shots, clicks land at the dot, a trigger pull on the pill under the picture does not move the screen but a grip pull does, each shot buzzes the hand. `lightgun_reload = true`: pulling the trigger with the gun pointed off the picture is a right click while held. `lightgun = off` gives the old behaviour with the gun model.
2. Gyro: `pad_gyro = controller`, start a stream, switch to gamepad mode. `moonlight.log`: `gamepad gyro from the controller`, then `the host asks for the gyroscope (N Hz)` once the PC wants it. On the PC the controller shows as a DualShock 4 (a PlayStation controller in Steam's controller list). In Steam Input, set gyro to mouse or joystick and turn the right controller; try `head`. Try `pad_gyro_ratchet = stick_touch`: the log's bindings line says `with the right stick's touch`; the gyro moves only with the right thumb resting on the stick and stops dead when it lifts.

## Open question: does the host make a DualShock 4?
Steam Input on the PC only sees native gyro from a device that has it. That needs Apollo to emulate a DualShock 4 (ViGEmBus) for the arrival we send (`LI_CTYPE_PS` with gyro and accelerometer). Not yet known: whether Apollo 0.4.x asks for motion (`setMotionEventState`) for this arrival; whether its `gamepad` setting (Automatic / DS4 / X360) can force an Xbox 360 pad and ignore the type; whether Steam Input reads gyro from ViGEm's DualShock 4 at all; and the axis signs and scale Apollo gives the DualShock 4 report. Needs one test on the PC; no code here depends on the answer, but the guide tells users to leave Apollo's gamepad type on Automatic.

# Head and pointer input (lane wave4-headinput, PLAN 4.4, 4.5, 6.5, 4b.10)

Each item is a `[vr]` key that is off by default, mirrored on the launcher's Settings page under Input Settings. No cog row (the Display tab is full). Tests: `xrcore_headaim` and the other cores named below, `m4_headinput` (the input pass through the seams on the context), `ui_settings`, `tests/launcher/run_tests.sh`.

## 4.4 Head-look (`head_look`, `head_look_px_deg`)
- Shares head aim's code end to end: `updateHeadAim` in `xr/xr_input.c` asks `headMouseWho` (`xr/core/xr_headaim.c`) which of the two drives this frame, then runs the same `headAimStep` (jump and gap drops, recentre drop, pitch limit, carry) and the same `pointerNudge` for the ray. Head aim wins where it can act (head locked, no room, either mode), so its behaviour is unchanged; head-look takes pointer mode with the screen head locked or a room up, at `head_look_px_deg` (head aim's lane 1..30, default 8) and head aim's dead zone. A switch between the two resets the step so no carry crosses rates.
- The pause: `bindPointerStep` sets `ctx->headLookHold` from `headLookHeld`: any held `app:lookhold` (new binding command, `XR_BIND_APP_LOOKHOLD`), or a grip down while the pointer table binds it to `none` (the default; grips are already taken out while the ray is on our UI). The head reads it the next frame (one frame late, the head runs ahead of the controllers). A pause is `HEAD_AIM_PAUSED`, so the head is measured afresh after it. Let go with everything else (`bindLetGo`).
- The head is located through the `locateSpace` seam now (it was `xrLocateSpace` directly), which is what `m4_headinput` drives; `xrHeadMouseFrame` runs the head's part of a frame for the tests.
- Log: `head-look on: N px a degree, dead zone D deg/s`, `head-look paused while its hold is down`, `head-look moved the mouse X, Y px in N s`, `head-look off`, as head aim's lines.

## 4.5 Head pose to opentrack (`opentrack`, `opentrack_host`, `opentrack_port`)
- Packer: `xr/core/xr_opentrack.c` (`xrcore_opentrack`, byte-exact): the head in the local space as OpenXR has it, metres to centimetres, the angles as R = Ry(yaw) Rx(pitch) Rz(roll) (yaw positive left, pitch up, roll tilting left), six little-endian doubles; a non-finite value goes out as 0.
- Sender: `backend/opentrack.c` (`backend_test_opentrack`, a loopback socket as opentrack). The backend already owns the sockets to the PC (netpath, mDNS); `mlxr_xrui` now links `mlxr_backend` for it. One non-blocking UDP socket (`SOCK_NONBLOCK`, `MSG_DONTWAIT`), the lock taken with `trylock` so the frame loop never waits; an address target is parsed at once (`AI_NUMERICHOST`), a name is resolved on a detached thread and packets are dropped until it lands. A send error is logged once per target and the sender keeps trying (opentrack may start later).
- Frame: `xrOpentrackFrame` in `xr/xr_input.c`, every frame ahead of the early returns while the setting is on, `ctx->streaming` and the session running, the view located through the `locateSpace` seam; a frame without a placed and turned head sends nothing. The stream ending closes the socket (`opentrack stopped after N packets`).
- The stream's PC: `mlxr_opentrack_set_stream_host(addr)` is the empty `opentrack_host`'s target. `app/main.c` has to call it with the stream's address when a stream starts and with `""` when it stops (two lines, left to the owner of `app/main.c`); until then only an explicit `opentrack_host` sends.
