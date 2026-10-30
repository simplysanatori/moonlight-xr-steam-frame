// Native (lane a11y-2): see xr_assistclick.h. GPLv3.
#include "xr_assistclick.h"

#include <stddef.h>

void auxClickReset(AuxClick* g) {
    g->down = 0;
    g->downNs = 0;
    g->upNs = 0;
    g->taps = 0;
    g->dragging = 0;
    g->escaped = 0;
    g->spent = 0;
}

void auxClickSpend(AuxClick* g) {
    if (g->down) {
        g->spent = 1;
        g->taps = 0;
    }
}

static int tapsAction(int taps) {
    return taps >= 3 ? ASSIST_RIGHT : taps == 2 ? ASSIST_DOUBLE : taps == 1 ? ASSIST_LEFT : ASSIST_NONE;
}

int auxClickEdge(AuxClick* g, int down, int64_t t) {
    if (down) {
        if (g->down) {
            return ASSIST_NONE;   // a repeat
        }
        g->down = 1;
        g->downNs = t;
        g->dragging = 0;
        g->escaped = 0;
        g->spent = 0;
        return ASSIST_NONE;
    }
    if (!g->down) {
        return ASSIST_NONE;
    }
    g->down = 0;
    if (g->escaped || g->spent) {
        g->escaped = 0;
        g->spent = 0;
        return ASSIST_NONE;
    }
    if (g->dragging) {
        g->dragging = 0;
        return ASSIST_DRAG_UP;
    }
    // a press that ended past the hold without a tick seeing it is still a tap
    g->taps++;
    g->upNs = t;
    if (g->taps >= 3) {
        g->taps = 0;
        return ASSIST_RIGHT;
    }
    return ASSIST_NONE;
}

int auxClickTick(AuxClick* g, int64_t now) {
    if (g->down) {
        int64_t held = now - g->downNs;
        if (!g->escaped && held >= AUXCLICK_ESCAPE_NS) {
            g->escaped = 1;
            g->dragging = 0;
            g->taps = 0;
            return ASSIST_ESCAPE;
        }
        if (!g->dragging && !g->escaped && !g->spent && held >= AUXCLICK_HOLD_NS) {
            // the taps before a hold are dropped: tap and hold is a drag, not a click and a drag
            g->dragging = 1;
            g->taps = 0;
            return ASSIST_DRAG_DOWN;
        }
        return ASSIST_NONE;
    }
    if (g->taps > 0 && now - g->upNs >= AUXCLICK_MULTI_NS) {
        int a = tapsAction(g->taps);
        g->taps = 0;
        return a;
    }
    return ASSIST_NONE;
}

const char* assistActionName(int action) {
    switch (action) {
    case ASSIST_LEFT: return "click";
    case ASSIST_DOUBLE: return "double click";
    case ASSIST_RIGHT: return "right click";
    case ASSIST_DRAG_DOWN: return "drag start";
    case ASSIST_DRAG_UP: return "drag end";
    case ASSIST_ESCAPE: return "handed back to the system";
    default: return "none";
    }
}

void assistPlayReset(AssistPlay* p) {
    p->action = ASSIST_NONE;
    p->startNs = 0;
    p->dragging = 0;
    p->lastButtons = 0;
}

void assistPlayStart(AssistPlay* p, int action, int64_t t) {
    switch (action) {
    case ASSIST_LEFT:
    case ASSIST_DOUBLE:
    case ASSIST_RIGHT:
        p->action = action;
        p->startNs = t;
        break;
    case ASSIST_DRAG_DOWN:
        p->action = ASSIST_NONE;
        p->dragging = 1;
        break;
    case ASSIST_DRAG_UP:
    case ASSIST_ESCAPE:
        p->dragging = 0;
        break;
    default:
        break;
    }
}

static int playing(const AssistPlay* p, int64_t now) {
    if (p->action == ASSIST_NONE || now < p->startNs) {
        return 0;
    }
    int64_t at = now - p->startNs;
    switch (p->action) {
    case ASSIST_LEFT: return at < ASSIST_PRESS_NS ? ASSIST_BTN_LEFT : 0;
    case ASSIST_RIGHT: return at < ASSIST_PRESS_NS ? ASSIST_BTN_RIGHT : 0;
    case ASSIST_DOUBLE:
        return at < ASSIST_PRESS_NS || (at >= 2 * ASSIST_PRESS_NS && at < 3 * ASSIST_PRESS_NS) ? ASSIST_BTN_LEFT : 0;
    default: return 0;
    }
}

static int64_t playEnd(const AssistPlay* p) {
    return p->startNs + (p->action == ASSIST_DOUBLE ? 3 : 1) * ASSIST_PRESS_NS;
}

int assistPlayButtons(AssistPlay* p, int64_t now, int* press) {
    int b = playing(p, now) | (p->dragging ? ASSIST_BTN_LEFT : 0);
    if (p->action != ASSIST_NONE && now >= playEnd(p)) {
        p->action = ASSIST_NONE;
    }
    if (press != NULL) {
        *press = (b & ~p->lastButtons) != 0;
    }
    p->lastButtons = b;
    return b;
}

int assistPlayBusy(const AssistPlay* p, int64_t now) {
    return p->dragging || (p->action != ASSIST_NONE && now < playEnd(p));
}
