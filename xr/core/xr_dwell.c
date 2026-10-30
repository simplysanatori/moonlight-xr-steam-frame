// Native (lane wave4-headinput, PLAN 6.5): see xr_dwell.h
#include "xr_dwell.h"

#include <math.h>

void dwellReset(Dwell* d) {
    d->charging = 0;
    d->x = 0.0f;
    d->y = 0.0f;
    d->startNs = 0;
    d->spent = 0;
}

int dwellMsClamp(int ms) {
    if (ms <= 0) {
        return 0;
    }
    return ms < DWELL_MS_MIN ? DWELL_MS_MIN : (ms > DWELL_MS_MAX ? DWELL_MS_MAX : ms);
}

static int wandered(const Dwell* d, float x, float y, float toleranceDeg) {
    float dx = x - d->x, dy = y - d->y;
    return dx * dx + dy * dy > toleranceDeg * toleranceDeg;
}

int dwellStep(Dwell* d, int valid, float x, float y, int64_t nowNs, int dwellMs, float toleranceDeg,
              float* fraction) {
    if (fraction != 0) {
        *fraction = -1.0f;
    }
    if (dwellMs <= 0 || !valid || !isfinite(x) || !isfinite(y)) {
        dwellReset(d);
        return DWELL_NONE;
    }
    if (d->spent) {
        if (!wandered(d, x, y, toleranceDeg)) {
            return DWELL_NONE;
        }
        d->spent = 0;
        d->charging = 0;
    }
    if (!d->charging || wandered(d, x, y, toleranceDeg)) {
        d->charging = 1;
        d->x = x;
        d->y = y;
        d->startNs = nowNs;
    }
    int64_t ran = nowNs - d->startNs;
    int64_t need = (int64_t)dwellMs * 1000000LL;
    if (ran >= need) {
        d->charging = 0;
        d->spent = 1;
        if (fraction != 0) {
            *fraction = 1.0f;
        }
        return DWELL_CLICK;
    }
    if (fraction != 0) {
        float part = ran <= 0 ? 0.0f : (float)((double)ran / (double)need);
        // a hair short of the click rounds to 1 in a float; charging stays under it
        *fraction = part < 0.9999f ? part : 0.9999f;
    }
    return DWELL_CHARGING;
}

float dwellRingScale(float fraction) {
    if (!(fraction >= 0.0f)) {
        return 0.0f;
    }
    if (fraction > 1.0f) {
        fraction = 1.0f;
    }
    return 3.0f - 2.0f * fraction;
}
