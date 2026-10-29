// One row of the ray recorder's CSV (MLXR_RAY_DEBUG, docs/dev/ray-debug.md):
// what the runtime gave for the head and both hands in one input frame, raw
// and in the local space, and what the frame made of it. The recorder writes
// it on the headset; the replay test (tests/m4/test_replay.c) reads it back
// and feeds it through the same input code. Plain text in and out, nothing
// else, so the host tests reach all of it.
//
// Native (lane ray-glitch).

#ifndef XR_RAYLOG_H
#define XR_RAYLOG_H

#include <stddef.h>
#include <stdint.h>

#include <openxr/openxr.h>

// Bits of RayLogHand.buttons: the buttons other than the trigger and grip
#define RAYLOG_BTN_RIGHT_CLICK  0x1
#define RAYLOG_BTN_MIDDLE_CLICK 0x2
#define RAYLOG_BTN_STICK_CLICK  0x4

typedef struct {
    // What is on the hand, PROFILE_ from xr_gate.h
    int kind;
    // The aim's locate succeeded, its location flags, and the action live
    int ok;
    unsigned flags;
    int active;
    // The aim pose as located, before the screen's frame or any filter
    XrPosef pose;
    // The joints found the hand with its index knuckle placed, and where that
    // knuckle was, in the local space
    int joints;
    XrVector3f knuckle;
    float trigger;
    float grip;
    XrVector2f stick;
    unsigned buttons;
    // What the frame made of it: the ray's source (AIM_ from xr_ray.h) and
    // whether the controller is awake on its own clock
    int src;
    int awake;
} RayLogHand;

typedef struct {
    // The input frame's clock, nanoseconds
    int64_t t;
    // The screen is in the head's frame (head locked, no room)
    int toHead;
    // The head as located, local space
    unsigned headFlags;
    XrPosef head;
    // Left then right
    RayLogHand hand[2];
    // What the frame decided: the shared pointer awake, the source that
    // points (-1 none, 0 left, 1 right, 2 gaze), the hand whose ray is drawn
    // (-1 none) and what the pointing source is over (HOVER_)
    int pointer;
    int chosen;
    int ray;
    int hover;
} RayLogRow;

// The header line, without the newline. Says the length written.
int rayLogHeader(char* buf, size_t size);

// One row, without the newline. Says the length written, 0 if it did not fit.
int rayLogFormat(const RayLogRow* row, char* buf, size_t size);

// Reads one line back. Says 1 for a row, 0 for the header, a blank line, a
// comment (#) or anything that is not a whole row.
int rayLogParse(const char* line, RayLogRow* row);

#endif
