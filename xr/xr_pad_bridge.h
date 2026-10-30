// Moonlight XR Native: gamepad mode's pad to the host (M3). The frame loop's
// settled pad state goes out through input/pad_out (LiSendMultiControllerEvent);
// the host's rumble comes back through input/rumble. GPLv3.
#pragma once
#include <stdint.h>

#include "core/xr_gamepad.h"
#include "../input/pad_out.h"

void xrPadBridgeSetRumble(int mix, int intensityPercent); // settings: rumble_mix, rumble_intensity
void xrPadBridgeSetFlip(int flip);          // settings: swap A/B and X/Y
// Native (lane headinput-finish, PLAN 4b.10): [vr] back_as_guide, Back (View) goes to the host as Guide
// (xr_gamepad.h padBackAsGuide), after the disconnect chord has seen it. Live.
void xrPadBridgeSetBackAsGuide(int on);
void xrPadBridgeStart(void);                // stream start, before LiStartConnection
void xrPadBridgeFrame(int plugged, const PadState* s, int64_t nowNs); // each input frame
void xrPadBridgeStop(void);                 // before LiStopConnection: unplug the pad, stop rumble
// Native (lane wave3-session): the headset came off. The pad goes to the host at rest at once (it stays
// plugged in), so no button or stick is left held there; the next frame's state replaces it. A no-op when
// the pad is not plugged in.
void xrPadBridgeRest(void);

// Native (lane wave2-lightgun-gyro, PLAN 2.9): the pad_gyro opt-in. On, the pad's next arrival says it is a
// PlayStation pad with a gyroscope and an accelerometer (and only then; off it is the Xbox pad with neither),
// the host's setMotionEventState for the pad's number is answered, and motion goes out through
// xrPadBridgeMotion. A pad already plugged in keeps the arrival it made. Read at the next stream start too.
void xrPadBridgeSetMotion(int on);
// What the host asked for in Hz, 0 when nothing: motionType is PADOUT_MOTION_ACCEL or _GYRO
int xrPadBridgeMotionRate(int motionType);
// One sample: the gyroscope in deg/s or the accelerometer in m/s^2, see input/pad_out.h padOutMotion.
// Returns 1 when it went to the host.
int xrPadBridgeMotion(int motionType, float x, float y, float z, int64_t nowNs);
// The host's motion request as the stream layer passes it on (gamepadsHostMotionState); public for the tests
void xrPadBridgeHostMotionState(uint16_t controllerNumber, uint8_t motionType, uint16_t reportRateHz);

// The disconnect chord: Start + Select (Back) + both triggers past 80 percent,
// held together in gamepad mode. On the frame it completes the pad goes to the
// host at rest (so nothing is left stuck there), then fn(user) is called once,
// outside this module's lock. The pad stays at rest until the chord is let go,
// so one press fires once. fn may be NULL.
void xrPadBridgeSetDisconnectHook(void (*fn)(void* user), void* user);
// As xrPadBridgeStart with the sends given (tests)
void xrPadBridgeStartWithSink(const PadOutSink* sink);
