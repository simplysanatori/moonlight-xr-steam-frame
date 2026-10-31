// Native (lane radial-dwell): see xr_radial.h. GPLv3.
#include "xr_radial.h"

#include <math.h>
#include <stddef.h>

#include "xr_assistclick.h"

int radialSectorAt(float right, float up, int current) {
    float r2 = right * right + up * up;
    if (!(r2 <= RADIAL_CANCEL_DEG * RADIAL_CANCEL_DEG)) {
        return RADIAL_OUT;   // NaN too
    }
    // in a segment already: it holds until the dot is clearly back in the middle
    float inner = current >= 0 ? RADIAL_DEAD_DEG - RADIAL_EDGE_SLACK_DEG : RADIAL_DEAD_DEG;
    if (r2 <= inner * inner) {
        return RADIAL_NONE;
    }
    // four quarters round the middle, their edges on the diagonals; up is the gap
    if (fabsf(right) >= fabsf(up)) {
        return right < 0.0f ? RADIAL_LEFT : RADIAL_RIGHT;
    }
    return up < 0.0f ? RADIAL_DOWN : RADIAL_NONE;
}

int radialSectorAction(int sector) {
    switch (sector) {
    case RADIAL_LEFT: return ASSIST_DOUBLE;
    case RADIAL_DOWN: return ASSIST_DRAG_DOWN;
    case RADIAL_RIGHT: return ASSIST_RIGHT;
    default: return ASSIST_NONE;
    }
}

void radialReset(RadialDwell* r) {
    r->open = 0;
    r->ax = r->ay = 0.0f;
    r->startNs = r->openNs = 0;
    r->sector = RADIAL_NONE;
    r->sectorNs = 0;
}

static void blank(RadialView* v) {
    v->fraction = -1.0f;
    v->open = 0;
    v->menu = 0.0f;
    v->sector = RADIAL_NONE;
    v->hold = 0.0f;
    v->fresh = 0;
}

// A click or a choice: the dwell spent where the dot is now, as dwellStep leaves it after a click
static void spend(RadialDwell* r, Dwell* d, float x, float y) {
    d->charging = 0;
    d->spent = 1;
    d->x = x;
    d->y = y;
    r->open = 0;
    r->sector = RADIAL_NONE;
}

static float clamp01(float t) {
    return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
}

int radialStep(RadialDwell* r, Dwell* d, int valid, int aimed, float x, float y, int64_t nowNs, int dwellMs,
               float toleranceDeg, int menu, int* action, RadialView* view) {
    RadialView none;
    if (view == NULL) {
        view = &none;
    }
    blank(view);
    if (action != NULL) {
        *action = ASSIST_NONE;
    }
    // the dwell under it dropped from outside (the pointer unwatched, a new pointer source)
    if (r->open && (!d->charging || d->startNs != r->startNs)) {
        radialReset(r);
    }
    if (!r->open) {
        float fraction = -1.0f;
        int what = dwellStep(d, valid, x, y, nowNs, dwellMs, toleranceDeg, &fraction);
        view->fresh = d->charging && d->startNs == nowNs;
        if (what == DWELL_CLICK) {
            if (!menu) {
                return RADIAL_EV_CLICK;
            }
            // a frame so late the dwell ran from under the menu to its end: nothing was chosen, so
            // it is the left click on the dwell's point
            if (action != NULL) {
                *action = ASSIST_LEFT;
            }
            r->sector = RADIAL_NONE;
            return RADIAL_EV_ACTION;
        }
        if (what != DWELL_CHARGING) {
            return RADIAL_EV_NONE;
        }
        view->fraction = fraction;
        if (!menu || fraction < RADIAL_OPEN_AT) {
            return RADIAL_EV_NONE;
        }
        r->open = 1;
        r->ax = d->x;
        r->ay = d->y;
        r->startNs = d->startNs;
        r->openNs = nowNs;
        r->sector = RADIAL_NONE;
        r->sectorNs = nowNs;
    }
    // the menu is up: the dwell's own wander no longer counts, only where the dot is in the menu
    if (!aimed || dwellMs <= 0 || !isfinite(x) || !isfinite(y)) {
        dwellReset(d);
        radialReset(r);
        return RADIAL_EV_CANCEL;
    }
    int s = radialSectorAt(x - r->ax, y - r->ay, r->sector);
    if (s == RADIAL_OUT) {
        dwellReset(d);
        radialReset(r);
        return RADIAL_EV_CANCEL;
    }
    if (s != r->sector) {
        r->sector = s;
        r->sectorNs = nowNs;
    }
    int64_t need = (int64_t)dwellMs * 1000000LL;
    int64_t ran = nowNs - r->startNs;
    view->open = 1;
    view->menu = clamp01((float)((double)(nowNs - r->openNs) / (double)RADIAL_MENU_FADE_NS));
    if (s >= 0) {
        int64_t held = nowNs - r->sectorNs;
        if (held >= RADIAL_HOLD_NS) {
            if (action != NULL) {
                *action = radialSectorAction(s);
            }
            spend(r, d, x, y);
            blank(view);
            return RADIAL_EV_ACTION;
        }
        view->sector = s;
        view->hold = clamp01((float)((double)held / (double)RADIAL_HOLD_NS));
        return RADIAL_EV_NONE;
    }
    // the middle, or the gap above it: the dwell runs on to its end, which is the left click
    if (ran >= need) {
        if (action != NULL) {
            *action = ASSIST_LEFT;
        }
        spend(r, d, x, y);
        blank(view);
        return RADIAL_EV_ACTION;
    }
    float part = ran <= 0 ? 0.0f : (float)((double)ran / (double)need);
    view->fraction = part < 0.9999f ? part : 0.9999f;
    return RADIAL_EV_NONE;
}

// ---- the art's frames ----------------------------------------------------------------------------

float radialRingProgress(float fraction) {
    if (!(fraction >= RADIAL_RING_DELAY)) {
        return -1.0f;
    }
    return clamp01((fraction - RADIAL_RING_DELAY) / (1.0f - RADIAL_RING_DELAY));
}

static int stepOf(float progress, int steps) {
    if (!(progress >= 0.0f)) {
        return -1;
    }
    int i = (int)(progress * (float)steps);
    return i < steps ? i : steps - 1;
}

int radialRingFrame(float progress) {
    return stepOf(progress, RADIAL_RING_STEPS);
}

int radialHollowFrame(float progress) {
    return stepOf(progress, RADIAL_HOLLOW_STEPS);
}

int radialFadeFrame(float progress, int64_t elapsedNs) {
    if (!(progress >= 0.0f) || elapsedNs < 0 || elapsedNs >= RADIAL_RING_FADE_NS) {
        return -1;
    }
    int level = (int)(elapsedNs * RADIAL_FADE_LEVELS / RADIAL_RING_FADE_NS);
    return RADIAL_FRAME_FADE + stepOf(progress, RADIAL_FADE_BUCKETS) * RADIAL_FADE_LEVELS + level;
}

float radialFadeLevel(int level) {
    // the circle at its full strength the frame it is dropped, then these, then gone
    static const float L[RADIAL_FADE_LEVELS] = { 0.62f, 0.36f, 0.16f };
    return level >= 0 && level < RADIAL_FADE_LEVELS ? L[level] : 0.0f;
}

int radialMenuFrame(const RadialView* v) {
    if (v == NULL || !v->open) {
        return -1;
    }
    if (v->sector >= 0 && v->sector < RADIAL_SECTORS) {
        return RADIAL_MENU_FADE_STEPS + v->sector * RADIAL_HOLD_STEPS + stepOf(v->hold, RADIAL_HOLD_STEPS);
    }
    // the fade in: the first step the frame it opens
    int f = (int)(v->menu * (float)RADIAL_MENU_FADE_STEPS);
    return f < RADIAL_MENU_FADE_STEPS ? f : RADIAL_MENU_FADE_STEPS - 1;
}

float radialMenuScale(const RadialView* v) {
    if (v == NULL || !v->open) {
        return 0.0f;
    }
    if (v->sector >= 0) {
        return 1.0f;
    }
    float t = clamp01(v->menu);
    // ease out: quick at first, settling into place
    float e = 1.0f - (1.0f - t) * (1.0f - t);
    return RADIAL_MENU_GROW + (1.0f - RADIAL_MENU_GROW) * e;
}
