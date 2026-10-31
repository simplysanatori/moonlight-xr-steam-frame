// Native (lane a11y-tab): see xr_access.h. GPLv3.
#include "xr_access.h"

#include <math.h>
#include <stddef.h>

#include "xr_dwell.h"
#include "xr_layout.h"
#include "xr_shared.h"

static const int DWELL_MS[ACCESS_DWELL_CELLS] = { 0, 600, 900, 1200, 1500 };

int accessDwellMs(int cell) {
    return cell >= 0 && cell < ACCESS_DWELL_CELLS ? DWELL_MS[cell] : -1;
}

int accessDwellCell(int ms) {
    if (ms <= 0) {
        return 0;
    }
    for (int i = 1; i < ACCESS_DWELL_CELLS; i++) {
        if (DWELL_MS[i] == ms) {
            return i;
        }
    }
    return -1;
}

int accessEyesState(int offered, int bound, int seen) {
    // a gaze that arrived is the real signal, whatever else was found
    if (seen) {
        return ACCESS_EYES_READY;
    }
    if (!offered) {
        return ACCESS_EYES_UNAVAILABLE;
    }
    return bound ? ACCESS_EYES_WAITING : ACCESS_EYES_NO_ACTION;
}

int accessEyesCellLive(int eyesState, int pointerSource) {
    return eyesState != ACCESS_EYES_UNAVAILABLE || pointerSource == POINTER_SOURCE_GAZE;
}

int accessButtonGreyed(int state) {
    return state == ACCESS_BUTTON_MISSING || state == ACCESS_BUTTON_UNREADABLE;
}

int accessDotClamp(int percent) {
    return percent < ACCESS_DOT_MIN ? ACCESS_DOT_MIN : percent > ACCESS_DOT_MAX ? ACCESS_DOT_MAX : percent;
}

float accessDotScale(int percent, int pointerSource) {
    if (pointerSource != POINTER_SOURCE_HEAD && pointerSource != POINTER_SOURCE_GAZE) {
        return 1.0f;
    }
    return (float)accessDotClamp(percent) / 100.0f;
}

int accessRowAt(float v) {
    for (int row = 0; row < COG_ACCESS_ROW_COUNT; row++) {
        if (fabsf(v - cogRowV(COG_TAB_ACCESS, row)) < cogRowHalf(COG_TAB_ACCESS)) {
            return row;
        }
    }
    return -1;
}

void gazeFallReset(GazeFall* g) {
    g->head = 1;
    g->sinceNs = 0;
    g->lastUsable = 0;
    g->started = 0;
}

int gazeFallStep(GazeFall* g, int gaze, int64_t nowNs, int* changed) {
    int was = g->head;
    int usable = gaze > 0;
    if (!g->started || usable != g->lastUsable) {
        g->started = 1;
        g->lastUsable = usable;
        g->sinceNs = nowNs;
    }
    int64_t held = nowNs - g->sinceNs;
    if (gaze < 0) {
        g->head = 1;
    }
    else if (usable && g->head && held >= GAZEFALL_BACK_NS) {
        g->head = 0;
    }
    else if (!usable && !g->head && held > GAZEFALL_LOST_NS) {
        g->head = 1;
    }
    if (changed != NULL) {
        *changed = g->head != was;
    }
    return g->head;
}

// ---- lane a11y-final ------------------------------------------------------------------------------

float accessDwellToleranceDeg(int pointerSource) {
    return pointerSource == POINTER_SOURCE_GAZE ? DWELL_TOLERANCE_EYES_DEG : DWELL_TOLERANCE_DEG;
}

static void unit3(float v[3]) {
    float n = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (n > 1e-9f) {
        v[0] /= n;
        v[1] /= n;
        v[2] /= n;
    }
}

static void copy3(float to[3], const float from[3]) {
    to[0] = from[0];
    to[1] = from[1];
    to[2] = from[2];
}

// The angle between two unit directions, in degrees
static float degBetween(const float a[3], const float b[3]) {
    float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    d = d > 1.0f ? 1.0f : (d < -1.0f ? -1.0f : d);
    return acosf(d) * (180.0f / (float)M_PI);
}

// a moved toward b by t (0..1) and kept a unit direction: fine at the small angles of a frame
static void toward(float a[3], const float b[3], float t) {
    for (int i = 0; i < 3; i++) {
        a[i] += (b[i] - a[i]) * t;
    }
    unit3(a);
}

// How far a first order low pass with time constant tau moves in dt
static float blendFor(float dt, float tau) {
    return dt > 0.0f ? 1.0f - expf(-dt / tau) : 0.0f;
}

void gazeSteadyReset(GazeSteady* g) {
    g->valid = 0;
    g->following = 0;
    g->away = 0;
    for (int i = 0; i < 3; i++) {
        g->smooth[i] = 0.0f;
        g->dot[i] = 0.0f;
        g->awaySum[i] = 0.0f;
    }
}

void gazeSteadyStep(GazeSteady* g, const float raw[3], float dt, float out[3]) {
    float r[3];
    copy3(r, raw);
    unit3(r);
    if (!g->valid || !isfinite(r[0]) || !isfinite(r[1]) || !isfinite(r[2])) {
        if (isfinite(r[0]) && isfinite(r[1]) && isfinite(r[2])) {
            gazeSteadyReset(g);
            g->valid = 1;
            copy3(g->smooth, r);
            copy3(g->dot, r);
        }
        copy3(out, g->valid ? g->dot : r);
        return;
    }
    if (degBetween(r, g->smooth) > GAZE_STEADY_JUMP_DEG) {
        // a saccade, if the next frame agrees: the smoothing goes straight to where the eyes went
        for (int i = 0; i < 3; i++) {
            g->awaySum[i] += r[i];
        }
        if (++g->away >= 2) {
            copy3(g->smooth, g->awaySum);
            unit3(g->smooth);
            g->away = 0;
            g->awaySum[0] = g->awaySum[1] = g->awaySum[2] = 0.0f;
        }
    }
    else {
        g->away = 0;
        g->awaySum[0] = g->awaySum[1] = g->awaySum[2] = 0.0f;
        toward(g->smooth, r, blendFor(dt, GAZE_STEADY_SMOOTH_S));
    }
    // the dot holds through a fixation and catches up once the gaze has really moved
    if (!g->following && degBetween(g->dot, g->smooth) > GAZE_STEADY_HOLD_DEG) {
        g->following = 1;
    }
    if (g->following) {
        toward(g->dot, g->smooth, blendFor(dt, GAZE_STEADY_FOLLOW_S));
        if (degBetween(g->dot, g->smooth) < GAZE_STEADY_SETTLE_DEG) {
            g->following = 0;
        }
    }
    copy3(out, g->dot);
}

void pointerBackReset(PointerBack* b) {
    b->prev = 1;
    b->pressing = 0;
    b->pressNs = 0;
    b->gripSeen = 0;
    b->holding = 0;
    b->sinceNs = 0;
    b->fired = 0;
    b->passing = 0;
}

int pointerBackStep(PointerBack* b, int menu, int grip, int64_t nowNs, int* menuOut) {
    int fire = 0, out = 0;
    menu = menu != 0;
    if (menu) {
        if (!b->prev) {
            b->pressing = 1;
            b->pressNs = nowNs;
            b->gripSeen = 0;
            b->holding = 0;
            b->fired = 0;
            b->passing = 0;
        }
        if (b->pressing) {
            if (grip && !b->passing) {
                b->gripSeen = 1;
                if (!b->holding) {
                    b->holding = 1;
                    b->sinceNs = nowNs;
                }
                if (!b->fired && nowNs - b->sinceNs >= POINTER_BACK_HOLD_NS) {
                    b->fired = 1;
                    fire = 1;
                }
            }
            else {
                // the grip let go starts the next hold afresh
                b->holding = 0;
            }
            if (!b->gripSeen && !b->passing && nowNs - b->pressNs >= POINTER_BACK_HOLD_NS) {
                b->passing = 1;
            }
            out = b->passing;
        }
    }
    else {
        // a short press of Menu alone: the press it was kept from being, now
        out = b->pressing && !b->gripSeen && !b->passing;
        b->pressing = 0;
        b->holding = 0;
    }
    b->prev = menu;
    if (menuOut != NULL) {
        *menuOut = out;
    }
    return fire;
}
