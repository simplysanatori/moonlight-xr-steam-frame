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
Triggers: padTrigger -> 0..255 (13 percent dead zone) -> unsigned char. Sticks: padStick -> -32766..32766 -> short, y up positive. No guide/SPECIAL button, no paddles.

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
