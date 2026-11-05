// Native (lane a11y-tab): the Accessibility tab's pure parts. The dwell row's cells and the times
// they stand for, the pointer row's Eyes cell, what the eye tracking line says, the eyes pointer's
// fallback to the head while the eyes are lost, and (lane a11y-2) the size of the head and eye
// pointer's dot and which row of the tab a point on it is on; (lane a11y-final) the right hand's
// chord that gives the pointer back to the controllers, the Eyes pointer's steadying filter and the
// dwell's tolerance for each pointer. Plain arithmetic, no OpenXR and no context, so tests/xrcore
// reaches all of it. GPLv3.
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

// ---- the headset button's device (lane a11y-2) -----------------------------------------------------
// Lane a11y-final: the headset button is a future feature (docs/dev/accessibility.md); the tab no
// longer has its row, and these are kept for when it comes back.
#define ACCESS_BUTTON_UNKNOWN 0     // not looked for yet
#define ACCESS_BUTTON_OK 1          // found and readable
#define ACCESS_BUTTON_MISSING 2     // no gpio-keys device with KEY_SELECT
#define ACCESS_BUTTON_UNREADABLE 3  // found, but this user cannot read it
// Whether the tab greys its row for the device: missing or unreadable (unknown is live)
int accessButtonGreyed(int state);

// ---- the tab's rows (lane a11y-2) ------------------------------------------------------------------
// The row of the tab a point at v (0 top, 1 bottom of the panel) is on, live or greyed, label or
// cells, or -1 between and past them: the row the footer describes
int accessRowAt(float v);
// Native (lane eye-calibration): the Eye calibration row acts only while eye gaze reaches the app
// (ACCESS_EYES_READY; otherwise it shows the eye tracking line's text), its Start whenever the row
// does, its Reset only with a calibration stored
int accessEyeCalLive(int eyesState);
int accessEyeCalCellLive(int eyesState, int cell, int stored);

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

// ---- the dwell's tolerance (lane a11y-final) --------------------------------------------------------
// How far the dot may wander, in degrees, before a dwell starts again: DWELL_TOLERANCE_DEG for the
// head, more for the eyes, which never rest as still as a head does
#define DWELL_TOLERANCE_EYES_DEG 2.0f
float accessDwellToleranceDeg(int pointerSource);

// ---- the Eyes pointer's steadiness (lane a11y-final) -----------------------------------------------
// Eye gaze jitters by about a degree even while the eyes rest on one spot. The Eyes pointer's dot is
// steadied for that, at the cost of speed: the gaze is smoothed (GAZE_STEADY_SMOOTH_S), and the dot
// holds still while the smoothed gaze stays within GAZE_STEADY_HOLD_DEG of it (a fixation); past
// that it catches up (GAZE_STEADY_FOLLOW_S) until it is within GAZE_STEADY_SETTLE_DEG and holds
// again. A gaze GAZE_STEADY_JUMP_DEG from the smoothed one on two frames running is a saccade: the
// smoothing jumps there at once (a single wild frame is ignored), and the dot follows.
#define GAZE_STEADY_HOLD_DEG 1.0f
#define GAZE_STEADY_SETTLE_DEG 0.2f
#define GAZE_STEADY_SMOOTH_S 0.12f
#define GAZE_STEADY_FOLLOW_S 0.04f
#define GAZE_STEADY_JUMP_DEG 4.0f

typedef struct {
    int valid;
    float smooth[3];   // the smoothed gaze, a unit direction
    float dot[3];      // where the dot points, a unit direction
    int following;     // the dot is catching up with the smoothed gaze
    int away;          // frames in a row the gaze has been a jump away
    float awaySum[3];  // and their sum
} GazeSteady;

void gazeSteadyReset(GazeSteady* g);

// One frame of the gaze: raw a unit direction, dt the frame's time in seconds. out the direction
// the dot points (the first frame after a reset: raw itself). out may be raw.
void gazeSteadyStep(GazeSteady* g, const float raw[3], float dt, float out[3]);

// ---- the right hand's way back to the controllers (lane a11y-final) ------------------------------------
// With the head or the eyes pointing, the right Menu button and the right grip held together for
// POINTER_BACK_HOLD_NS give the pointer back to the controllers, as the left View button and the
// left grip switch to gamepad mode. Menu alone stays the cog's: while it is down it is kept from the
// bindings, and let go before POINTER_BACK_HOLD_NS with the grip never held it is a press there and
// then (a tap, one frame down); held alone that long it goes through as held from then on. With the
// grip held at any time during the press, Menu does nothing but the chord.
#define POINTER_BACK_HOLD_NS 500000000LL

typedef struct {
    int prev;          // the menu button on the last step (1 after a reset: a press under way is not one)
    int pressing;      // a press that started while this was watching
    int64_t pressNs;   // when it started
    int gripSeen;      // the grip was held at some time during it
    int holding;       // both held, and since when
    int64_t sinceNs;
    int fired;         // the chord went during this press
    int passing;       // Menu held alone past the hold time: through as held
} PointerBack;

void pointerBackReset(PointerBack* b);

// One frame of the right Menu button and the right grip. Returns 1 on the frame the two have been
// held together for POINTER_BACK_HOLD_NS, once a press. *menuOut is what the bindings see of Menu.
int pointerBackStep(PointerBack* b, int menu, int grip, int64_t nowNs, int* menuOut);

#endif
