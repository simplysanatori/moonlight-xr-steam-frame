// Native (lane pointer-clicks): press stabilisation, double-click wait and the
// pointer's button mapping. Pure: no OpenXR, no clock of its own.
//
// Pulling a trigger jolts the aim by a few pixels, and Windows wants both
// clicks of a double click inside its double-click rectangle (4x4 px at 100%
// scaling) and 500 ms. Normal pointing is never delayed: the cursor is live
// at every frame except around a click.
//  - Press stabilisation: a short history of the smoothed aim point is kept;
//    when a button goes down it is sent at the point from just before the
//    trigger started to move (the touch of the pull), not where the jolt left
//    the aim. The cursor stays on that point only until the aim leaves a small
//    circle (3 px, under Windows' drag slop); from then on it is live at once,
//    so a drag is not held back. A release that never left the circle is sent
//    at the press point too, so a click stays a click.
//  - Double-click wait (Native (lane double-click)): after a click's release
//    the cursor stays where the release left it while the aim is within
//    MLXR_DOUBLE_CLICK_PX (24) pixels of it, until MLXR_DOUBLE_CLICK_MS (500)
//    after that click's press, which is when Windows stops counting a second
//    press as a double click. The aim leaving the circle or the time running
//    out lets the cursor go to the aim at once. A press during the wait goes
//    at exactly the point the first click went down at, with no motion sent
//    between the two clicks, and stays there (its release too) until the aim
//    leaves the circle, when it goes live (a drag). A press whose aim went
//    further than the circle while held was a drag and starts no wait.
#ifndef MLXR_XR_PRESSFIX_H
#define MLXR_XR_PRESSFIX_H

#include <stdint.h>

#define PRESSFIX_HISTORY 32
#define PRESSFIX_LEAD_NS 25000000LL      // before the touch of the pull
#define PRESSFIX_MAX_BACK_NS 100000000LL // a slow half pull is not a jolt: look back at most this far
#define PRESSFIX_DEADZONE_PX 3.0f        // live again once the aim is this far from the press point
// Native (lane double-click): 24 px and 500 ms. The 5 px snap of 2026-10-11
// missed second clicks whose aim had drifted further between the clicks.
#define PRESSFIX_DOUBLE_DEFAULT_PX 24.0f
#define PRESSFIX_DOUBLE_DEFAULT_MS 500.0f // Windows' default double-click time

typedef struct {
    int64_t t[PRESSFIX_HISTORY];
    float u[PRESSFIX_HISTORY];
    float v[PRESSFIX_HISTORY];
    int count;        // samples held, up to PRESSFIX_HISTORY
    int next;         // where the next goes
    int prevTouching;
    int64_t touchNs;  // when the current pull began; 0 when not touching
    int down;
    int left;         // the aim left the circle during the press: live
    float radiusPx;
    float anchorU;
    float anchorV;
    int64_t pressNs;  // when the button held now went down
    float reachPx;    // the farthest the aim got from the anchor while held
    float lastU;      // the position handed back last frame
    float lastV;
    float prevU;      // where the last click left the cursor
    float prevV;
    float prevDownU;  // where it went down, which a second press is sent at
    float prevDownV;
    int64_t prevPressNs; // when that click went down
    int waiting;      // after a click: the cursor held for a second click
} PressFix;

typedef struct {
    int stabilise;      // press stabilisation on
    float doublePx;     // double-click radius in host pixels, 0 = off
    int64_t doubleNs;   // double-click time, press to press, 0 = off
} PressFixCfg;

void pressFixReset(PressFix* p);

// One frame with the ray on the picture. (u, v) is the smoothed aim point, 0..1
// across and down, on a picture of width x height pixels. touching says the
// trigger is being pulled (touched or down), down that any mouse button is
// held. The position to send comes back in outU and outV (they may alias u, v).
void pressFixStep(PressFix* p, int touching, int down, float u, float v, int width, int height,
                  PressFixCfg cfg, int64_t nowNs, float* outU, float* outV);

// MLXR_PRESS_STABILISE: 0 turns it off, anything else (or unset) leaves it on
int pressStabiliseFromEnv(void);
// MLXR_DOUBLE_CLICK_PX: a radius in host pixels, 0 turns the double-click wait
// off; unset, empty, negative or not a number is the default
float doubleClickPxFromEnv(void);
// MLXR_DOUBLE_CLICK_MS: the double-click time in ms, 0 turns the wait off;
// unset, empty, negative, over 5000 or not a number is the default
float doubleClickMsFromEnv(void);
// MLXR_STICK_RIGHT_CLICK: 0 gives the stick click back its old job of
// switching the pointer on and off; anything else (or unset) makes it a right click
int stickRightClickFromEnv(void);

// The host buttons the controllers ask for, as VR_BUTTON_* bits (1 left, 2
// right, 4 middle). The trigger of either hand is the left button unless it was
// spent on a panel; the right and middle buttons (A/B, d-pad) and the sticks'
// clicks (when stickRight) are the pointer's alone: in gamepad mode they are
// the pad's.
typedef struct {
    int triggerDown[2];
    int triggerSwallowed[2];
    int padMode;
    int rightButton;   // A or d-pad down
    int middleButton;  // B or d-pad right
    int stickClick[2]; // as the gamepad shortcut leaves them
    int stickRight;
} PointerButtons;

int pointerButtonMask(const PointerButtons* b);

#endif
