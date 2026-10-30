// Moonlight XR Native: gamepad mode's pad to the host (M3). The frame loop's
// settled pad state goes out through input/pad_out (LiSendMultiControllerEvent);
// the host's rumble comes back through input/rumble. GPLv3.
#pragma once
#include <stdint.h>

#include "core/xr_gamepad.h"
#include "../input/pad_out.h"

void xrPadBridgeSetRumble(int mix, int intensityPercent); // settings: rumble_mix, rumble_intensity
void xrPadBridgeSetFlip(int flip);          // settings: swap A/B and X/Y
void xrPadBridgeStart(void);                // stream start, before LiStartConnection
void xrPadBridgeFrame(int plugged, const PadState* s, int64_t nowNs); // each input frame
void xrPadBridgeStop(void);                 // before LiStopConnection: unplug the pad, stop rumble
// Native (lane wave3-session): the headset came off. The pad goes to the host at rest at once (it stays
// plugged in), so no button or stick is left held there; the next frame's state replaces it. A no-op when
// the pad is not plugged in.
void xrPadBridgeRest(void);

// The disconnect chord: Start + Select (Back) + both triggers past 80 percent,
// held together in gamepad mode. On the frame it completes the pad goes to the
// host at rest (so nothing is left stuck there), then fn(user) is called once,
// outside this module's lock. The pad stays at rest until the chord is let go,
// so one press fires once. fn may be NULL.
void xrPadBridgeSetDisconnectHook(void (*fn)(void* user), void* user);
// As xrPadBridgeStart with the sends given (tests)
void xrPadBridgeStartWithSink(const PadOutSink* sink);
