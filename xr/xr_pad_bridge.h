// Moonlight XR Native: gamepad mode's pad to the host (M3). The frame loop's
// settled pad state goes out through input/pad_out (LiSendMultiControllerEvent);
// the host's rumble comes back through input/rumble. GPLv3.
#pragma once
#include <stdint.h>

#include "core/xr_gamepad.h"

void xrPadBridgeSetFlip(int flip);          // settings: swap A/B and X/Y
void xrPadBridgeStart(void);                // stream start, before LiStartConnection
void xrPadBridgeFrame(int plugged, const PadState* s, int64_t nowNs); // each input frame
void xrPadBridgeStop(void);                 // before LiStopConnection: unplug the pad, stop rumble
