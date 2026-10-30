// Native (lane wave2-bind-wire): the binding table (xr_bind.h) at work frame
// to frame. What the controls down hold this frame, and what went down and
// came up since the last one, so a key goes down once and up once however many
// controls share it, an app command fires once a press, and everything held is
// let go together on a mode switch, a panel or focus going. Plus the table
// helpers the input pass needs: a whole stick's wasd, arrows or dpad spread
// over its four directions, the Guide chord handed to padGuideStep's hold, and
// a line for the log of what differs from the defaults. Plain arithmetic, no
// OpenXR and no context, so tests/xrcore reaches all of it. GPLv3.
#ifndef XR_BINDRUN_H
#define XR_BINDRUN_H

#include <stddef.h>
#include <stdint.h>

#include "xr_bind.h"

// The most actions a frame can hold: every control's own, every chord's and
// the disconnect chord's
#define XR_BIND_HELD_MAX (XR_BIND_SRC_COUNT + XR_BIND_MAX_CHORDS + 1)

typedef struct {
    // Controls held back until they are let go: down when everything was let
    // go (a mode switch, a panel, focus), so the press that did it is not the
    // new table's
    uint64_t ignore;
    // What was held last frame, each action once
    int count;
    XrBindAction held[XR_BIND_HELD_MAX];
} XrBindRun;

typedef struct {
    // What is held this frame, each action once
    int heldCount;
    XrBindAction held[XR_BIND_HELD_MAX];
    // What went down and what came up since last frame
    int pressedCount;
    XrBindAction pressed[XR_BIND_HELD_MAX];
    int releasedCount;
    XrBindAction released[XR_BIND_HELD_MAX];
} XrBindEdges;

void xrBindRunReset(XrBindRun* r);

// One frame of the table t with the controls down (xrBindSourcesDown's bits,
// as the caller has them), less those held back
void xrBindRunStep(XrBindRun* r, const XrBindTable* t, uint64_t down, XrBindEdges* out);

// Everything held let go (each in out's released), and the controls in down
// held back until let go
void xrBindRunLetGo(XrBindRun* r, uint64_t down, XrBindEdges* out);

// The host mouse buttons held, VR_BUTTON_ bits (1 << the mouse: code)
int xrBindEdgesMouse(const XrBindEdges* e);
// The pad buttons held (PAD_ bits), and in *pulled the PAD_HELD_LT and
// PAD_HELD_RT bits a held pad: action pulls all the way
int xrBindEdgesPad(const XrBindEdges* e, int* pulled);

// Each whole stick bound to stick:wasd, arrows or dpad gives its four
// directions the keys W A S D, the arrow keys, or the pad's d-pad (the arrow
// keys in pointer mode, where there is no pad), where a direction is bound to
// nothing of its own
void xrBindTableExpandSticks(XrBindTable* t);

// Whether two tables do the same: every control's action and the same chords
// bound to the same actions (where they came from aside)
int xrBindTableSame(const XrBindTable* a, const XrBindTable* b);

// Gamepad mode's Guide is left View with right Menu held a quarter second
// (xr_gamepad.h padGuideStep), which the table alone cannot time. When t's
// chord of the two is pad:guide and they are still Back and Start on their own,
// the chord is taken out of t, so the two come through as Back and Start for
// padGuideStep to make Guide of, and 1 is returned; else t is left alone and
// any chord of theirs fires at once.
int xrBindTableGuideHold(XrBindTable* t);

// What t binds differently from the defaults of its mode, as the file would
// say it ("right.y = key:F, left.view+right.menu = none"), or "" for nothing.
// Returns how many differ.
int xrBindSummary(const XrBindTable* t, char* out, size_t cap);

#endif
