// Native (lane a11y-tab): the Accessibility tab's pure parts. The dwell row's cells and the times
// they stand for, the pointer row's Eyes cell, what the eye tracking line says, the eyes pointer's
// fallback to the head while the eyes are lost, and (lane a11y-2) the size of the head and eye
// pointer's dot and which row of the tab a point on it is on. Plain arithmetic, no OpenXR and no
// context, so tests/xrcore reaches all of it. GPLv3.
#ifndef XR_ACCESS_H
#define XR_ACCESS_H

#include <stdint.h>

#include "xr_dwell.h"   // POINTER_SOURCE_*

// ---- the dwell row ------------------------------------------------------------------------------
// Off, 0.6 s, 0.9 s, 1.2 s, 1.5 s: 0.9 s is the safe start, 0.6 s for practised users
#define ACCESS_DWELL_CELLS 5

// The head_dwell_ms a cell writes (0 for Off; -1 out of range)
int accessDwellMs(int cell);
// The cell a stored head_dwell_ms is ringed on: 0 for off (0 or less), the cell of exactly that
// time, else -1 (a time config.ini set that no cell has: none is ringed)
int accessDwellCell(int ms);

// ---- eye tracking ---------------------------------------------------------------------------------
// What the eye tracking line says, from what the session found. Native (lane a11y-2): the app asks
// for eye gaze whenever the runtime offers it, so there is no "not asked for" any more.
#define ACCESS_EYES_READY 0        // eye gaze reached the app this session
#define ACCESS_EYES_WAITING 1      // the gaze action is attached, no gaze has arrived yet
#define ACCESS_EYES_NO_ACTION 2    // offered, but the gaze action could not be set up
#define ACCESS_EYES_UNAVAILABLE 3  // the runtime does not offer XR_EXT_eye_gaze_interaction
#define ACCESS_EYES_COUNT 4

// offered: the runtime lists XR_EXT_eye_gaze_interaction; bound: the extension was enabled and the
// gaze action and its binding were taken; seen: a tracked gaze pose arrived this session. A gaze
// that arrived is ready whatever the rest says. SteamVR's supportsEyeGazeInteraction is only a
// hint (it has read 0 on systems that gave gaze), so it plays no part.
int accessEyesState(int offered, int bound, int seen);

// Whether the pointer row's Eyes cell can be pressed: always, unless the runtime does not offer eye
// gaze at all (then it is drawn dimmed), and always when Eyes is the setting already (so what is
// set can be seen; it points with the head until the eyes come)
int accessEyesCellLive(int eyesState, int pointerSource);

// ---- the dot's size (lane a11y-2) -----------------------------------------------------------------
// [vr] assist_dot_size: the head and eye pointer's dot (and its dwell ring) as a percentage of the
// size it has always had, 50 to 300 in the slider's 25 % steps; 100 is that size
#define ACCESS_DOT_MIN 50
#define ACCESS_DOT_MAX 300
#define ACCESS_DOT_STEP 25
#define ACCESS_DOT_DEFAULT 100
#define ACCESS_DOT_STEPS ((ACCESS_DOT_MAX - ACCESS_DOT_MIN) / ACCESS_DOT_STEP)

// A stored percentage held to 50..300 (a value between the steps is kept as it is)
int accessDotClamp(int percent);
// The scale the dot's side is drawn at: percent / 100 with the head or the eyes pointing, 1 with
// the controllers (their dot is not this setting's)
float accessDotScale(int percent, int pointerSource);

// ---- the tab's rows (lane a11y-2) ------------------------------------------------------------------
// The row of the tab a point at v (0 top, 1 bottom of the panel) is on, live or greyed, label or
// cells, or -1 between and past them: the row the footer describes
int accessRowAt(float v);

// ---- the eyes pointer's fallback --------------------------------------------------------------------
// With the pointer on Eyes, the dot follows the gaze while it is tracked. Lost for longer than
// GAZEFALL_LOST_NS, the head's ray points instead; tracked again for GAZEFALL_BACK_NS, the eyes
// take it back. No eye gaze at all is the head from the start.
#define GAZEFALL_LOST_NS 200000000LL
#define GAZEFALL_BACK_NS 300000000LL

typedef struct {
    int head;          // 1: the head points, 0: the eyes do
    int64_t sinceNs;   // when the gaze last changed between usable and not (0: not yet)
    int lastUsable;    // the gaze the last step saw
    int started;
} GazeFall;

void gazeFallReset(GazeFall* g);

// One frame: gaze is 1 for a tracked gaze pose, 0 for none this frame, -1 for no eye gaze at all.
// Returns 1 for the head, 0 for the eyes; *changed (may be NULL) is 1 on the frame it switches.
int gazeFallStep(GazeFall* g, int gaze, int64_t nowNs, int* changed);

#endif
