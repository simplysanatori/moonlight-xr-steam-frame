// Native (lane wave2-bind-wire): the thumbstick wound into a smooth, high
// resolution wheel (PLAN 2.7, report 15 section e). The host's wheel takes
// 120 to a notch (LiSendHighResScrollEvent, LiSendHighResHScrollEvent), so the
// tilt goes out a little every frame rather than a whole notch every few:
// a page glides instead of stepping. Up and right are positive, as both
// calls take them. Plain arithmetic, no clock of its own. GPLv3.
#ifndef XR_SCROLL_H
#define XR_SCROLL_H

// The scroll_speed setting's SCROLL_SPEED_ bounds
#include "xr_shared.h"

// One wheel notch in the host's high resolution units
#define SCROLL_HIRES_NOTCH 120

typedef struct {
    // What did not make a whole unit yet, vertical then horizontal
    float carry[2];
} ScrollAcc;

void scrollAccReset(ScrollAcc* a);

// One axis of a stick: the tilt past the dead zone, from 0 there to 1 at full
// tilt, times perSec units a second over dt seconds, wound into whole units
// with the rest carried to the next frame. Under the dead zone the carry is
// dropped, so letting go never leaves a part unit to come out later.
int stickWind(float* carry, float tilt, float dt, float perSec, float deadzone);

// A stick into this frame's wheel, in high resolution units, notchesPerSec
// at full tilt. The two axes wind on their own, but an axis is left out while
// the other is tilted more than twice as far, so scrolling a page up and down
// does not drift it sideways.
void scrollAccStep(ScrollAcc* a, float x, float y, float dt, float notchesPerSec, float deadzone,
                   int* outV, int* outH);

// The setting held to SCROLL_SPEED_MIN..MAX
int scrollSpeedClamp(int percent);
// Notches a second at full tilt for the setting, baseNotchesPerSec at 100
float scrollNotchesPerSec(int percent, float baseNotchesPerSec);

#endif
