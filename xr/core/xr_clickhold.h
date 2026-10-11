// Click hold: the host cursor stays where it was when a press starts, until
// the aim has moved a few host pixels (then it is a drag), and for a moment
// after the release so a second click of a double click lands on the same
// point. Pulling a trigger jolts the aim by a few pixels, which Windows (4 px
// click slop) otherwise reads as a drag, or as two clicks too far apart.
// Pure: no OpenXR, no clock of its own.
#ifndef MLXR_XR_CLICKHOLD_H
#define MLXR_XR_CLICKHOLD_H

#include <stdint.h>

#define CLICK_HOLD_DEFAULT_PX 8.0f
#define CLICK_HOLD_WINDOW_NS 350000000LL // after the release, for the second click

typedef struct {
    int seeded;       // a position was passed through before
    float lastU;      // the last position handed back
    float lastV;
    int prevTouching; // for the moment a press begins
    int holding;
    int dragging;     // the aim left the circle while pressed: live again
    float anchorU;
    float anchorV;
    int64_t untilNs;  // the hold ends at this time once released; 0 while pressed
} ClickHold;

void clickHoldReset(ClickHold* c);

// One frame with the ray on the picture. (u, v) is the smoothed aim point,
// 0..1 across and down, on a picture of width x height pixels; touching says
// the trigger (or another button) is touched or down. holdPx <= 0 turns the
// hold off. The position to send comes back in outU and outV.
void clickHoldStep(ClickHold* c, int touching, float u, float v, int width, int height,
                   float holdPx, int64_t nowNs, float* outU, float* outV);

// MLXR_CLICK_HOLD_PX: a number of host pixels, 0 to turn the hold off; anything
// else (unset, empty, negative, not a number) is the default
float clickHoldPixelsFromEnv(void);

#endif
