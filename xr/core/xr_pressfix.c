// Native (lane pointer-clicks): see xr_pressfix.h
#include "xr_pressfix.h"

#include <math.h>
#include <stdlib.h>

void pressFixReset(PressFix* p) {
    *p = (PressFix){ 0 };
}

int pressStabiliseFromEnv(void) {
    const char* s = getenv("MLXR_PRESS_STABILISE");
    return !(s != NULL && s[0] == '0' && s[1] == '\0');
}

float doubleClickPxFromEnv(void) {
    const char* s = getenv("MLXR_DOUBLE_CLICK_PX");
    if (s == NULL || *s == '\0') {
        return PRESSFIX_DOUBLE_DEFAULT_PX;
    }
    char* end = NULL;
    float v = strtof(s, &end);
    if (end == s || !(v >= 0.0f) || v > 1000.0f) {
        return PRESSFIX_DOUBLE_DEFAULT_PX;
    }
    return v;
}

// Native (lane double-click)
float doubleClickMsFromEnv(void) {
    const char* s = getenv("MLXR_DOUBLE_CLICK_MS");
    if (s == NULL || *s == '\0') {
        return PRESSFIX_DOUBLE_DEFAULT_MS;
    }
    char* end = NULL;
    float v = strtof(s, &end);
    if (end == s || !(v >= 0.0f) || v > 5000.0f) {
        return PRESSFIX_DOUBLE_DEFAULT_MS;
    }
    return v;
}

int stickRightClickFromEnv(void) {
    const char* s = getenv("MLXR_STICK_RIGHT_CLICK");
    return !(s != NULL && s[0] == '0' && s[1] == '\0');
}

int pointerButtonMask(const PointerButtons* b) {
    int mask = 0;
    for (int h = 0; h < 2; h++) {
        // Per hand rather than either hand, so a trigger spent on the grid or a
        // panel is out of the count while the other one still clicks
        if (b->triggerDown[h] && !b->triggerSwallowed[h]) {
            mask |= 0x1;
        }
    }
    if (!b->padMode) {
        if (b->rightButton) {
            mask |= 0x2;
        }
        if (b->middleButton) {
            mask |= 0x4;
        }
        if (b->stickRight && (b->stickClick[0] || b->stickClick[1])) {
            mask |= 0x2;
        }
    }
    return mask;
}

static void push(PressFix* p, int64_t now, float u, float v) {
    p->t[p->next] = now;
    p->u[p->next] = u;
    p->v[p->next] = v;
    p->next = (p->next + 1) % PRESSFIX_HISTORY;
    if (p->count < PRESSFIX_HISTORY) {
        p->count++;
    }
}

// The newest sample no later than t, or the oldest held when all are later
static void sampleAt(const PressFix* p, int64_t t, float* u, float* v) {
    int best = -1;
    int oldest = -1;
    for (int k = 0; k < p->count; k++) {
        // k = 0 is the newest
        int i = (p->next - 1 - k + 2 * PRESSFIX_HISTORY) % PRESSFIX_HISTORY;
        oldest = i;
        if (p->t[i] <= t) {
            best = i;
            break;
        }
    }
    int i = best >= 0 ? best : oldest;
    *u = p->u[i];
    *v = p->v[i];
}

static float distPx(float u0, float v0, float u1, float v1, int w, int h) {
    return hypotf((u1 - u0) * (float)w, (v1 - v0) * (float)h);
}

void pressFixStep(PressFix* p, int touching, int down, float u, float v, int width, int height,
                  PressFixCfg cfg, int64_t nowNs, float* outU, float* outV) {
    if (width <= 0 || height <= 0) {
        *outU = u;
        *outV = v;
        return;
    }
    push(p, nowNs, u, v);
    if (touching && !p->prevTouching) {
        p->touchNs = nowNs;
    }
    else if (!touching) {
        p->touchNs = 0;
    }
    p->prevTouching = touching;

    int press = down && !p->down;
    int release = !down && p->down;

    // Native (lane double-click): the wait after a click ends as soon as the
    // aim leaves the circle or a second press could no longer be a double
    // click, and the cursor goes to the aim at once. A press frame is judged
    // below instead, from the aim before the pull's jolt.
    if (p->waiting && !press
            && (nowNs - p->prevPressNs > cfg.doubleNs
                || distPx(u, v, p->prevU, p->prevV, width, height) > cfg.doublePx)) {
        p->waiting = 0;
    }

    if (press) {
        float au = u;
        float av = v;
        float radius = 0.0f;
        if (cfg.stabilise) {
            int64_t ref = nowNs - PRESSFIX_MAX_BACK_NS;
            if (p->touchNs != 0 && p->touchNs > ref) {
                ref = p->touchNs;
            }
            sampleAt(p, ref - PRESSFIX_LEAD_NS, &au, &av);
            radius = PRESSFIX_DEADZONE_PX;
        }
        // A second press while the cursor still waits on the last click goes
        // at exactly the point that click went down at, the point Windows
        // compares it with
        if (p->waiting && nowNs - p->prevPressNs <= cfg.doubleNs
                && (distPx(au, av, p->prevU, p->prevV, width, height) <= cfg.doublePx
                    || distPx(u, v, p->prevU, p->prevV, width, height) <= cfg.doublePx)) {
            au = p->prevDownU;
            av = p->prevDownV;
            if (cfg.doublePx > radius) {
                radius = cfg.doublePx;
            }
        }
        p->waiting = 0;
        p->down = 1;
        p->left = 0;
        p->radiusPx = radius;
        p->anchorU = au;
        p->anchorV = av;
        p->pressNs = nowNs;
        p->reachPx = 0.0f;
    }
    if (p->down) {
        // The press frame goes at the anchor whatever the jolt did, and a
        // release frame is the trigger letting go, which jolts too: whether
        // the aim left the circle is judged only while the button is held
        if (!press && !release) {
            float d = distPx(u, v, p->anchorU, p->anchorV, width, height);
            if (d > p->reachPx) {
                p->reachPx = d;
            }
            if (!p->left && d > p->radiusPx) {
                p->left = 1;
            }
        }
        if (release && p->left) {
            // Native (lane double-click): the release goes where the cursor
            // already is, not where the let-go jolted the aim
            *outU = p->lastU;
            *outV = p->lastV;
        }
        else if (p->left) {
            *outU = u;
            *outV = v;
        }
        else {
            *outU = p->anchorU;
            *outV = p->anchorV;
        }
        if (release) {
            p->down = 0;
            p->prevU = *outU;
            p->prevV = *outV;
            p->prevDownU = p->anchorU;
            p->prevDownV = p->anchorV;
            p->prevPressNs = p->pressNs;
            // Native (lane double-click): a click starts the wait when its
            // press is recent enough for a second one to count. A press that
            // wandered a few pixels (the jolt going on after the press) is
            // still a click; one that went past the circle is a drag.
            p->waiting = cfg.doublePx > 0.0f && cfg.doubleNs > 0 && p->reachPx <= cfg.doublePx
                    && nowNs - p->pressNs <= cfg.doubleNs;
        }
    }
    else if (p->waiting) {
        *outU = p->prevU;
        *outV = p->prevV;
    }
    else {
        *outU = u;
        *outV = v;
    }
    p->lastU = *outU;
    p->lastV = *outV;
}
