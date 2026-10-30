// Native (lane a11y-tab): the Accessibility tab's pure parts. The dwell row's cells and the times
// they stand for, which pointer cells the tab shows, what the eye tracking line says, and the eyes
// pointer's fallback to the head while the eyes are lost. Plain arithmetic, no OpenXR and no
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
// What the eye tracking line says, from what the session found
#define ACCESS_EYES_READY 0        // eye gaze reached the app this session
#define ACCESS_EYES_WAITING 1      // the runtime says it supports it, but no gaze has arrived yet
#define ACCESS_EYES_NOT_ASKED 2    // the runtime offers the extension, but nothing here asked for it
#define ACCESS_EYES_UNAVAILABLE 3  // the runtime does not give eye gaze to apps
#define ACCESS_EYES_COUNT 4

// offered: the runtime lists XR_EXT_eye_gaze_interaction; asked: [vr] gaze or pointer_source = gaze
// asked for it at the session's start; working: the extension was enabled, the system reports
// supportsEyeGazeInteraction and the gaze binding was taken; seen: a tracked gaze pose arrived this
// session. A gaze that arrived is ready whatever the rest says: SteamVR's flag is only a hint (it
// has read 0 on systems that gave gaze), so the input pass keeps the gaze action without it.
int accessEyesState(int offered, int asked, int working, int seen);

// Whether the pointer row shows its Eyes cell: when eye gaze has arrived this session, or when Eyes
// is the setting already (so what is set is never hidden; it points with the head until the eyes
// come). 3 cells or 2.
int accessPointerCells(int eyesState, int pointerSource);

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
