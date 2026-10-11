// Native (lane pointer-clicks): press stabilisation, double-click snap and the
// pointer's button mapping. Pure: no OpenXR, no clock of its own.
//
// Pulling a trigger jolts the aim by a few pixels, and Windows wants both
// clicks of a double click inside its double-click rectangle (4x4 px at 100%
// scaling) and 500 ms. Nothing here delays normal pointing: the cursor is live
// at every frame except around a press.
//  - Press stabilisation: a short history of the smoothed aim point is kept;
//    when a button goes down it is sent at the point from just before the
//    trigger started to move (the touch of the pull), not where the jolt left
//    the aim. The cursor stays on that point only until the aim leaves a small
//    circle (3 px, under Windows' drag slop); from then on it is live at once,
//    so a drag is not held back. A release that never left the circle is sent
//    at the press point too, so a click stays a click.
//  - Double-click snap: a press within 500 ms of the last click's release and
//    within MLXR_DOUBLE_CLICK_PX (5) host pixels of where it was sent goes at
//    exactly that point (and so does its release) until the aim leaves the
//    5 px circle, when it goes live (a drag).
#ifndef MLXR_XR_PRESSFIX_H
#define MLXR_XR_PRESSFIX_H

#include <stdint.h>

#define PRESSFIX_HISTORY 32
#define PRESSFIX_LEAD_NS 25000000LL      // before the touch of the pull
#define PRESSFIX_MAX_BACK_NS 100000000LL // a slow half pull is not a jolt: look back at most this far
#define PRESSFIX_DEADZONE_PX 3.0f        // live again once the aim is this far from the press point
#define PRESSFIX_DOUBLE_NS 500000000LL   // Windows' default double-click time
#define PRESSFIX_DOUBLE_DEFAULT_PX 5.0f // owner: 5 px (2026-10-11), was 24

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
    int prevValid;    // a click point to snap a second click to
    float prevU;
    float prevV;
    int64_t prevReleaseNs;
} PressFix;

typedef struct {
    int stabilise;      // press stabilisation on
    float doublePx;     // double-click snap radius in host pixels, 0 = off
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
// MLXR_DOUBLE_CLICK_PX: a radius in host pixels, 0 turns the snap off; unset,
// empty, negative or not a number is the default
float doubleClickPxFromEnv(void);
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
