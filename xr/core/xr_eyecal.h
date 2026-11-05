// Native (lane eye-calibration): the eye gaze calibration's pure parts. Twelve dots in a 4 x 3 grid
// over the picture, shown one at a time; for each, the gaze is left to settle, then read for a while,
// and kept only as a steady fixation (a dot that fails is tried once more, then skipped). Every
// sample is taken in the head's frame (the gaze's yaw and pitch, and the dot's direction from the
// head at that moment), so the head moving during the run does not matter. The fit is a least
// squares correction from the measured to the true yaw and pitch: affine (six numbers) by default,
// with a second-order term only where it clearly does better on dots left out of the fit; the worst
// dots (at most three) are dropped and the fit made again; the result is refused when it does not
// improve the mean error by a margin, or when it is extreme. Plain arithmetic, no OpenXR and no
// context, so tests/xrcore reaches all of it. GPLv3.
#ifndef XR_EYECAL_H
#define XR_EYECAL_H

#include <stddef.h>
#include <stdint.h>

// ---- the dots ----------------------------------------------------------------------------------
#define EYECAL_COLS 4
#define EYECAL_ROWS 3
#define EYECAL_DOTS (EYECAL_COLS * EYECAL_ROWS)
// The grid's inset from the picture's edges, as a fraction of its width and height, so the corners
// and the edges are both sampled
#define EYECAL_INSET 0.08f

// Where a dot (0 to EYECAL_DOTS - 1, row by row from the top left) is on the picture (u, v from its
// top left, 0 to 1)
void eyeCalTarget(int dot, float* u, float* v);
// The dot shown at a step of the run (0 to EYECAL_DOTS - 1): a fixed order that jumps across the
// grid, so each new dot is a clear look away from the last
int eyeCalOrder(int step);

// ---- one run -----------------------------------------------------------------------------------
// The eye is left this long to settle on a new dot, then read for EYECAL_COLLECT_NS
#define EYECAL_SETTLE_NS 400000000LL
#define EYECAL_COLLECT_NS 800000000LL
// Room for the samples of one dot (0.8 s at 144 Hz is 116)
#define EYECAL_SAMPLES_MAX 192
// A fixation: the samples within EYECAL_GATE_DEG of their median (a blink or a glance away is not)
// are at least EYECAL_KEEP_FRAC of the frames read and at least EYECAL_SAMPLES_MIN, their spread
// (root mean square distance from their mean) is under EYECAL_STEADY_DEG, and the gaze is within
// EYECAL_FAR_DEG of the dot (further, the user was looking elsewhere)
#define EYECAL_GATE_DEG 3.0f
#define EYECAL_KEEP_FRAC 0.6f
#define EYECAL_SAMPLES_MIN 12
#define EYECAL_STEADY_DEG 1.5f
#define EYECAL_FAR_DEG 15.0f
// The dot's size across, in degrees: it shrinks from EYECAL_DOT_START times this to this while the
// eye settles, which draws the eye to its middle
#define EYECAL_DOT_DEG 1.2f
#define EYECAL_DOT_START 2.5f

#define EYECAL_PHASE_IDLE 0
#define EYECAL_PHASE_SETTLE 1
#define EYECAL_PHASE_COLLECT 2

// What a step of the run did
#define EYECAL_STEP_NONE 0
#define EYECAL_STEP_NEXT 1       // a new dot (or the same one again, once)
#define EYECAL_STEP_FINISHED 2   // the last dot is done: eyeCalFit the points

// One dot's result: the gaze's yaw and pitch as measured and the dot's, in the head's frame, degrees
// (yaw positive to the right, pitch positive up)
typedef struct {
    float my, mp;
    float ty, tp;
} EyeCalPoint;

typedef struct {
    int phase;
    int step;      // in eyeCalOrder
    int retried;   // the dot shown is on its second try
    int64_t phaseNs;   // when the phase began, 0 before the first step
    int frames;        // frames read in the collect phase
    int n;
    float gy[EYECAL_SAMPLES_MAX], gp[EYECAL_SAMPLES_MAX];
    float ty[EYECAL_SAMPLES_MAX], tp[EYECAL_SAMPLES_MAX];
    EyeCalPoint points[EYECAL_DOTS];   // by dot (grid order)
    int have[EYECAL_DOTS];
    int good, retries, skipped;
} EyeCalRun;

void eyeCalRunStart(EyeCalRun* r);
void eyeCalRunStop(EyeCalRun* r);
int eyeCalRunUp(const EyeCalRun* r);
// One frame: gazeOk whether the gaze is tracked this frame, gy, gp its yaw and pitch in the head's
// frame, ty, tp the dot's. Returns EYECAL_STEP_*.
int eyeCalRunStep(EyeCalRun* r, int64_t nowNs, int gazeOk, float gy, float gp, float ty, float tp);
// The dot shown now (grid order), or -1 with no run up
int eyeCalRunDot(const EyeCalRun* r);
// The dot's size across this frame, in degrees
float eyeCalRunDotDeg(const EyeCalRun* r, int64_t nowNs);

// Whether n samples (the gaze's and the dot's yaw and pitch, read over frames frames) are a
// fixation, by the rules above; if so the means of the ones kept go in out
int eyeCalFixation(const float* gy, const float* gp, const float* ty, const float* tp, int n, int frames,
                   EyeCalPoint* out, float* spreadDeg);

// ---- the fit -----------------------------------------------------------------------------------
#define EYECAL_MODEL_NONE 0
#define EYECAL_MODEL_AFFINE 1
#define EYECAL_MODEL_QUAD 2
// The correction is true = measured + c0 + c1 u + c2 v (+ c3 u^2 + c4 u v + c5 v^2), per axis, in
// degrees, with u and v the measured yaw and pitch in units of EYECAL_UNIT_DEG
#define EYECAL_UNIT_DEG 10.0f
#define EYECAL_TERMS 6
// At least this many good dots, of which at most EYECAL_DROP_MAX are dropped: each time the worst
// dot's error is over EYECAL_DROP_RATIO times the median and over EYECAL_DROP_FLOOR_DEG
#define EYECAL_MIN_DOTS 8
#define EYECAL_DROP_MAX 3
#define EYECAL_DROP_RATIO 2.5f
#define EYECAL_DROP_FLOOR_DEG 1.0f
// The second-order term is tried with at least EYECAL_QUAD_MIN_DOTS dots, and kept only when its
// error on each dot left out of the fit (leave-one-out) averages under EYECAL_QUAD_RATIO times the
// affine's and at least EYECAL_QUAD_GAIN_DEG less
#define EYECAL_QUAD_MIN_DOTS 10
#define EYECAL_QUAD_RATIO 0.8f
#define EYECAL_QUAD_GAIN_DEG 0.15f
// Refused unless the mean error after is at most EYECAL_GAIN_RATIO of the one before and at least
// EYECAL_GAIN_MIN_DEG less; or when the correction scales by less than EYECAL_SCALE_MIN or more than
// EYECAL_SCALE_MAX anywhere it was sampled (or mirrors), or moves the middle of the view by more than
// EYECAL_OFFSET_MAX_DEG
#define EYECAL_GAIN_RATIO 0.8f
#define EYECAL_GAIN_MIN_DEG 0.2f
#define EYECAL_SCALE_MIN 0.7f
#define EYECAL_SCALE_MAX 1.4f
#define EYECAL_OFFSET_MAX_DEG 10.0f
// The measured gaze is clamped to this before the correction is worked out, so a gaze far outside
// the grid is not carried off by the second-order term
#define EYECAL_CLAMP_YAW_DEG 40.0f
#define EYECAL_CLAMP_PITCH_DEG 30.0f

typedef struct {
    int model;   // EYECAL_MODEL_*, NONE for no correction
    float c[2][EYECAL_TERMS];   // yaw's, then pitch's
    // The angular error on the dots used, before and after, degrees
    float meanBefore, maxBefore, meanAfter, maxAfter;
    int used, dropped;
} EyeCal;

// What a fit came to
#define EYECAL_OK 0
#define EYECAL_FAIL_FEW 1       // fewer than EYECAL_MIN_DOTS steady dots
#define EYECAL_FAIL_GAIN 2      // no clear improvement
#define EYECAL_FAIL_EXTREME 3   // the correction is too large
#define EYECAL_RESULTS 4

// Fits the dots marked in have (EYECAL_DOTS of them). Returns EYECAL_OK or EYECAL_FAIL_*; out has
// the fit and its errors either way (model NONE when nothing could be fitted).
int eyeCalFit(const EyeCalPoint* points, const int* have, EyeCal* out);
// The correction applied to a measured yaw and pitch (degrees); none with model NONE
void eyeCalApply(const EyeCal* cal, float yaw, float pitch, float* outYaw, float* outPitch);
// Whether a correction is within the bounds above (a fitted one, or one read from config.ini)
int eyeCalSane(const EyeCal* cal);
// The angle between two directions given as yaw and pitch, degrees
float eyeCalAngleDeg(float yaw1, float pitch1, float yaw2, float pitch2);
// A direction (x right, y up, -z ahead, unit) to and from yaw and pitch in degrees
void eyeCalDir(float yawDeg, float pitchDeg, float d[3]);
void eyeCalAngles(const float d[3], float* yawDeg, float* pitchDeg);

// ---- config.ini --------------------------------------------------------------------------------
// [vr] eye_calibration: "<version> <a|q> <coefficients> <mean after> <mean before> <max after>
// <max before>", the coefficients yaw's then pitch's (3 each for a, 6 each for q). "" for none.
#define EYECAL_VERSION 1
// Writes the text form (fits in 128 bytes); "" for model NONE. Returns 0 when cap is too small.
int eyeCalFormat(const EyeCal* cal, char* out, size_t cap);
// Reads it: 1 with a correction in out, 0 for "" or a text that is not one (another version, a
// wrong count, a number out of bounds), with out's model NONE
int eyeCalParse(const char* text, EyeCal* out);

#endif
