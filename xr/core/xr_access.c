// Native (lane a11y-tab): see xr_access.h. GPLv3.
#include "xr_access.h"

#include <stddef.h>

#include "xr_dwell.h"

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

int accessEyesState(int offered, int asked, int working, int seen) {
    // a gaze that arrived is the real signal, whatever the system's flag said
    if (seen) {
        return ACCESS_EYES_READY;
    }
    if (!offered) {
        return ACCESS_EYES_UNAVAILABLE;
    }
    if (!asked) {
        return ACCESS_EYES_NOT_ASKED;
    }
    if (!working) {
        return ACCESS_EYES_UNAVAILABLE;
    }
    return seen ? ACCESS_EYES_READY : ACCESS_EYES_WAITING;
}

int accessPointerCells(int eyesState, int pointerSource) {
    return eyesState == ACCESS_EYES_READY || pointerSource == POINTER_SOURCE_GAZE ? 3 : 2;
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
