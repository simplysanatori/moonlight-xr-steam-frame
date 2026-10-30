// Native (lane radial-dwell): the dwell click's radial menu, for the head and eye pointer. With
// [vr] assist_dwell_style = radial, a dwell on the picture opens a small menu round the point it
// started on once RADIAL_OPEN_AT of it has run: keep the dot in the middle (inside the dead zone)
// until the dwell ends and that is a left click; move it left, down or right into a segment and hold
// it there RADIAL_HOLD_NS and that segment's click goes (double click, drag, right click); up is a gap
// that picks nothing; past the outer edge the dwell is cancelled. Every click lands on the point the
// dwell started on (the caller holds the host's cursor there while the menu is up).
//
// Also the frames of the dwell's art (the circle that sweeps round and shrinks onto the dot, its
// fade when a dwell is dropped, the menu's fade in and its lit segments), so the drawing and the tests
// agree on which picture a state shows. Plain arithmetic, no OpenXR, no context. GPLv3.
#ifndef XR_RADIAL_H
#define XR_RADIAL_H

#include <stdint.h>

#include "xr_dwell.h"

// [vr] assist_dwell_style
#define DWELL_STYLE_SIMPLE 0
#define DWELL_STYLE_RADIAL 1
#define DWELL_STYLE_COUNT 2

// ---- the menu's geometry, in degrees from where the dwell started ------------------------------
// From how far into the dwell the menu is up
#define RADIAL_OPEN_AT 0.75f
// Inside this, the middle: no direction counts (well over the eyes' and the head's jitter)
#define RADIAL_DEAD_DEG 2.5f
// The segments' outer edge as drawn
#define RADIAL_RING_DEG 4.5f
// Past this the dwell is cancelled: 1.5 times the ring, room for an overshoot
#define RADIAL_CANCEL_DEG (1.5f * RADIAL_RING_DEG)
// Once in a segment, the dot must come this far back inside the dead zone to leave it, so a dot
// resting on the edge does not start the segment's hold over and over
#define RADIAL_EDGE_SLACK_DEG 0.3f
// How long the dot must stay in a segment for its click
#define RADIAL_HOLD_NS 250000000LL
// How long the menu takes to fade in
#define RADIAL_MENU_FADE_NS 120000000LL

// The segments (the order they are drawn in), and what a point is in
#define RADIAL_NONE (-1)   // the middle, or the gap above it
#define RADIAL_LEFT 0      // double click
#define RADIAL_DOWN 1      // drag
#define RADIAL_RIGHT 2     // right click
#define RADIAL_SECTORS 3
#define RADIAL_OUT (-2)    // past the outer edge

// What a point (right and up of where the dwell started, degrees) is in. current is the segment the
// dot was in, for the slack at the dead zone's edge
int radialSectorAt(float right, float up, int current);
// The click a segment stands for (ASSIST_DOUBLE, ASSIST_DRAG_DOWN, ASSIST_RIGHT), ASSIST_NONE else
int radialSectorAction(int sector);

// ---- the state machine -----------------------------------------------------------------------
typedef struct {
    int open;          // the menu is up
    float ax, ay;      // where the dwell started (the point every click lands on)
    int64_t startNs;   // when the dwell started
    int64_t openNs;    // when the menu came up
    int sector;        // the segment the dot is in (RADIAL_NONE for none)
    int64_t sectorNs;  // since when
} RadialDwell;

// What the step leaves for the drawing
typedef struct {
    float fraction;    // how far the dwell has run, 0..1, while its circle shows; -1 otherwise
    int open;          // the menu is up
    float menu;        // its fade in, 0..1
    int sector;        // the segment the dot is in, RADIAL_NONE
    float hold;        // how far that segment's hold has run, 0..1
    int fresh;         // a dwell started at this step, at (x, y)
} RadialView;

// What a step comes to
#define RADIAL_EV_NONE 0     // nothing, or a dwell charging
#define RADIAL_EV_CLICK 1    // a plain dwell's click (the simple style, our menus, a drag's release)
#define RADIAL_EV_ACTION 2   // the menu's choice, in *action (ASSIST_LEFT, DOUBLE, RIGHT, DRAG_DOWN)
#define RADIAL_EV_CANCEL 3   // the menu was left past its outer edge (or the pointer lost)

void radialReset(RadialDwell* r);

// One frame. d is the dwell under it (xr_dwell.h; reset from outside, the menu goes too). valid: the
// dot is on something a click lands on and nothing is held (the dwell's own rule); aimed: there is a
// pointer and nothing is held (all the open menu needs: the dot may leave the picture to pick). x, y
// the pointer's direction in degrees, x to the right and y up; dwellMs as dwellMsClamp gives it;
// toleranceDeg the dwell's own wander. menu: the menu may open (the radial style, the dot on the
// picture, no drag held). *action (may be NULL) gets the choice on RADIAL_EV_ACTION; *view (may be
// NULL) what to draw. After a click or a choice the dwell is spent where the dot is, as dwellStep's.
int radialStep(RadialDwell* r, Dwell* d, int valid, int aimed, float x, float y, int64_t nowNs, int dwellMs,
               float toleranceDeg, int menu, int* action, RadialView* view);

// ---- the art's frames ----------------------------------------------------------------------------
// The dwell's circle: a thin arc in the dot's colour that sweeps clockwise from the top as the dwell
// runs while its radius shrinks onto the dot; the dot is in every frame, so the dot and its circle
// are one quad of RADIAL_ART_SIDE times the dot's side. It shows from RADIAL_RING_DELAY of the
// dwell on (a short pause first, so a dot passing over things does not flicker rings), and fades
// out over RADIAL_RING_FADE_NS when the dwell is dropped.
#define RADIAL_ART_SIDE 3.0f
#define RADIAL_RING_DELAY 0.1f
#define RADIAL_RING_STEPS 40         // the sweep, filled dot
#define RADIAL_HOLLOW_STEPS 20       // the sweep round a hollow dot (a drag held)
#define RADIAL_FADE_BUCKETS 5        // where the dropped circle had got to
#define RADIAL_FADE_LEVELS 3         // and how far it has faded
#define RADIAL_RING_FADE_NS 150000000LL
// The circle's radius, a fraction of the quad's half side: from the start to just round the dot
#define RADIAL_RING_R_START 0.86f
#define RADIAL_RING_R_END 0.27f
// The dot inside, as the plain dot's art: its edge (alpha 0.5) at this fraction of the quad's half
#define RADIAL_DOT_R (0.6f / RADIAL_ART_SIDE)

// Frames of the dot's quad (ATLAS_DWELL_RING, then ATLAS_DWELL_HOLLOW for the hollow sweep)
#define RADIAL_FRAME_FADE RADIAL_RING_STEPS
#define RADIAL_FRAME_HOLLOW_DOT (RADIAL_FRAME_FADE + RADIAL_FADE_BUCKETS * RADIAL_FADE_LEVELS)
#define RADIAL_FRAME_MARK (RADIAL_FRAME_HOLLOW_DOT + 1)
#define RADIAL_RING_FRAMES (RADIAL_FRAME_MARK + 1)

// How far the circle has swept and shrunk (0 at its start, 1 on the dot) for a dwell's fraction; -1
// while it does not show
float radialRingProgress(float fraction);
// The frame of a charging dwell's circle (progress >= 0) round a filled dot, or -1
int radialRingFrame(float progress);
// The same round a hollow dot, a frame of ATLAS_DWELL_HOLLOW, or -1
int radialHollowFrame(float progress);
// The frame of a circle dropped at progress, elapsedNs ago, or -1 once it has gone
int radialFadeFrame(float progress, int64_t elapsedNs);
// The alpha a fade frame is drawn at
float radialFadeLevel(int level);

// The menu: one picture of the three segments for each step of the fade in, and one for each step
// of a lit segment's hold. Drawn RADIAL_MENU_HALF_DEG round its middle.
#define RADIAL_MENU_HALF_DEG 4.8f
#define RADIAL_MENU_FADE_STEPS 3
#define RADIAL_HOLD_STEPS 4
#define RADIAL_MENU_FRAMES (RADIAL_MENU_FADE_STEPS + RADIAL_SECTORS * RADIAL_HOLD_STEPS)
// While it fades in it also grows from this to its full size
#define RADIAL_MENU_GROW 0.92f

// The menu's frame for what a step left (-1: no menu), and the size it is drawn at (of its full)
int radialMenuFrame(const RadialView* v);
float radialMenuScale(const RadialView* v);

#endif
