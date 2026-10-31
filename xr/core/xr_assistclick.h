// Native (lane a11y-2): clicks for the head and eye pointer that do not come from a controller's
// trigger. Two pure parts, no OpenXR, no devices, no context:
//
// AuxClick, the gestures of one button (the headset's side button, read from evdev or from an
// OpenXR path): a press is a left click, two presses within AUXCLICK_MULTI_NS of each other a
// double click, three a right click; a press held past AUXCLICK_HOLD_NS holds the left button down
// (a drag) until it is let go; held past AUXCLICK_ESCAPE_NS it lets the drag go and asks for the
// button to be handed back to the system (the caller releases its grab for a while).
//
// AssistPlay, what such an action does to the mouse buttons over the next frames: a click is a
// press of ASSIST_PRESS_NS, a double click two of them ASSIST_PRESS_NS apart, a right click a press
// of the right button, a drag the left button down until it is let go. The caller asks it each
// frame which buttons are down and whether a press starts this frame (for its own menus). GPLv3.
#ifndef XR_ASSISTCLICK_H
#define XR_ASSISTCLICK_H

#include <stdint.h>

#define AUXCLICK_HOLD_NS 400000000LL
#define AUXCLICK_MULTI_NS 350000000LL
#define AUXCLICK_ESCAPE_NS 3000000000LL

// What a gesture comes to
#define ASSIST_NONE 0
#define ASSIST_LEFT 1
#define ASSIST_DOUBLE 2
#define ASSIST_RIGHT 3
#define ASSIST_DRAG_DOWN 4
#define ASSIST_DRAG_UP 5
#define ASSIST_ESCAPE 6   // a 3 s hold: any drag let go, the button handed back to the system

typedef struct {
    int down;          // the button is down now
    int64_t downNs;    // when it went down
    int64_t upNs;      // when the last short press ended
    int taps;          // short presses waiting for the multi-press window to close
    int dragging;      // this press is holding the left button
    int escaped;       // this press has asked for the escape; its release does nothing
    int spent;         // native (lane radial-dwell): this press did something else (let a drag go):
                       // no click and no drag of its own; a 3 s hold still escapes
} AuxClick;

void auxClickReset(AuxClick* g);
// The button went down (1) or up (0) at t. Call auxClickTick first for the frame's time.
int auxClickEdge(AuxClick* g, int down, int64_t t);
// Once a frame: the hold, the escape and the end of the multi-press window
int auxClickTick(AuxClick* g, int64_t now);
// Native (lane radial-dwell): the press that is down now was used for something else: its release
// and its hold come to nothing (the taps before it are dropped too)
void auxClickSpend(AuxClick* g);
// A name for the log
const char* assistActionName(int action);

// ---- what an action does to the mouse buttons ----------------------------------------------------
#define ASSIST_PRESS_NS 60000000LL
#define ASSIST_BTN_LEFT 1
#define ASSIST_BTN_RIGHT 2

typedef struct {
    int action;        // the click under way (ASSIST_LEFT, DOUBLE, RIGHT), or NONE
    int64_t startNs;
    int dragging;      // the left button held for a drag
    int lastButtons;   // what the last frame had down, for the press edge
} AssistPlay;

void assistPlayReset(AssistPlay* p);
// An action from a gesture (or the radial menu) at t; DRAG_UP and ESCAPE let a drag go
void assistPlayStart(AssistPlay* p, int action, int64_t t);
// The buttons down at now (ASSIST_BTN_*); *press (may be NULL) is 1 on a frame a button goes down
int assistPlayButtons(AssistPlay* p, int64_t now, int* press);
// Whether anything is still to be played or held
int assistPlayBusy(const AssistPlay* p, int64_t now);

#endif
