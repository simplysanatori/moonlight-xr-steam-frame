#include "xr_clickhold.h"

#include <math.h>
#include <stdlib.h>

void clickHoldReset(ClickHold* c) {
    *c = (ClickHold){ 0 };
}

float clickHoldPixelsFromEnv(void) {
    const char* s = getenv("MLXR_CLICK_HOLD_PX");
    if (s == NULL || *s == '\0') {
        return CLICK_HOLD_DEFAULT_PX;
    }
    char* end = NULL;
    float v = strtof(s, &end);
    if (end == s || !(v >= 0.0f) || v > 1000.0f) {
        return CLICK_HOLD_DEFAULT_PX;
    }
    return v;
}

void clickHoldStep(ClickHold* c, int touching, float u, float v, int width, int height,
                   float holdPx, int64_t nowNs, float* outU, float* outV) {
    int pressed = touching && !c->prevTouching;
    c->prevTouching = touching;
    if (holdPx <= 0.0f || width <= 0 || height <= 0) {
        c->holding = 0;
        c->dragging = 0;
        c->untilNs = 0;
        c->seeded = 1;
        c->lastU = *outU = u;
        c->lastV = *outV = v;
        return;
    }
    // A press holds the cursor where the host last saw it (one that began
    // before the ray was on the picture holds nothing)
    if (pressed && !c->holding && c->seeded) {
        c->holding = 1;
        c->dragging = 0;
        c->anchorU = c->lastU;
        c->anchorV = c->lastV;
        c->untilNs = 0;
    }
    if (c->holding) {
        float dx = (u - c->anchorU) * (float)width;
        float dy = (v - c->anchorV) * (float)height;
        if (dx * dx + dy * dy > holdPx * holdPx) {
            c->dragging = 1;
        }
        if (touching) {
            c->untilNs = 0; // a second press inside the window keeps the hold
        }
        else if (c->dragging || (c->untilNs != 0 && nowNs > c->untilNs)) {
            c->holding = 0;
            c->dragging = 0;
            c->untilNs = 0;
        }
        else if (c->untilNs == 0) {
            c->untilNs = nowNs + CLICK_HOLD_WINDOW_NS;
        }
    }
    if (c->holding && !c->dragging) {
        *outU = c->anchorU;
        *outV = c->anchorV;
    }
    else {
        *outU = u;
        *outV = v;
    }
    c->seeded = 1;
    c->lastU = *outU;
    c->lastV = *outV;
}
