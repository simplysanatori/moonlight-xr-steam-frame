// Native (lane wave4-headinput, PLAN 6.5): head "Point" mode's dwell click, and the pointer source
// setting it belongs to. The pointer rests on one spot for head_dwell_ms and that is a click; it
// has to move away and settle again for the next. The state machine is fed the pointer's direction
// in degrees (any two angles that are locally flat, such as the ray's yaw and pitch), the time and
// whether the pointer is on something that takes a click, and answers nothing, charging (with how
// far) or click. Plain arithmetic, no OpenXR and no context, so tests/xrcore reaches all of it and
// a later lane can add click types and a pause without touching the input pass. GPLv3.
#ifndef XR_DWELL_H
#define XR_DWELL_H

#include <stdint.h>

// [vr] pointer_source: what points. controller is the controllers' rays (and gaze when the gaze
// switch is on, as before); head is the view's own ray, One Euro filtered, with either hand's
// trigger clicking; gaze is the eye-gaze pointer (the same as gaze = true)
#define POINTER_SOURCE_CONTROLLER 0
#define POINTER_SOURCE_HEAD 1
#define POINTER_SOURCE_GAZE 2
#define POINTER_SOURCE_COUNT 3

// head_dwell_ms: 0 is off, otherwise held to DWELL_MS_MIN..MAX
#define DWELL_MS_MIN 400
#define DWELL_MS_MAX 2000
// How far the pointer may wander, in degrees, before a dwell starts again
#define DWELL_TOLERANCE_DEG 1.0f
// How long the click it makes is held down, in nanoseconds: long enough for every path that reads a
// press on a frame edge, short enough to be a click
#define DWELL_PRESS_NS 60000000LL

// What one step says
#define DWELL_NONE 0
#define DWELL_CHARGING 1
#define DWELL_CLICK 2

typedef struct {
    // A dwell is under way from (x, y) since startNs
    int charging;
    float x;
    float y;
    int64_t startNs;
    // A click went at (x, y): nothing more until the pointer leaves it
    int spent;
} Dwell;

void dwellReset(Dwell* d);

// head_dwell_ms as the input pass uses it: 0 (or less) off, else DWELL_MS_MIN..MAX
int dwellMsClamp(int ms);

// One frame. valid: the pointer is on something a click can land on and nothing is held. x, y the
// pointer's direction in degrees, nowNs the time, dwellMs as dwellMsClamp gives it (0: always
// DWELL_NONE, and the state forgets), toleranceDeg how far it may wander. *fraction (may be NULL)
// is how far the dwell has run, 0..1, while charging, 1 on the click and -1 otherwise. An invalid
// frame drops the dwell. After a click nothing charges until the pointer has moved past the
// tolerance from where it clicked, so staring at one spot clicks once.
int dwellStep(Dwell* d, int valid, float x, float y, int64_t nowNs, int dwellMs, float toleranceDeg,
              float* fraction);

// The dot's ring while a dwell charges: its size over the dot's, closing from 3 to 1 as fraction
// goes 0 to 1; 0 (no ring) for a fraction below 0
float dwellRingScale(float fraction);

#endif
