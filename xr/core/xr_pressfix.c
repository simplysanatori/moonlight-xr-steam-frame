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
        if (cfg.doublePx > 0.0f && p->prevValid && nowNs - p->prevReleaseNs <= PRESSFIX_DOUBLE_NS
                && (distPx(au, av, p->prevU, p->prevV, width, height) <= cfg.doublePx
                    || distPx(u, v, p->prevU, p->prevV, width, height) <= cfg.doublePx)) {
            au = p->prevU;
            av = p->prevV;
            if (cfg.doublePx > radius) {
                radius = cfg.doublePx;
            }
        }
        p->down = 1;
        p->left = 0;
        p->radiusPx = radius;
        p->anchorU = au;
        p->anchorV = av;
    }
    if (p->down) {
        // The press frame goes at the anchor whatever the jolt did, and a
        // release frame is the trigger letting go, which jolts too: whether
        // the aim left the circle is judged only while the button is held
        if (!press && !release && !p->left && distPx(u, v, p->anchorU, p->anchorV, width, height) > p->radiusPx) {
            p->left = 1;
        }
        if (p->left) {
            *outU = u;
            *outV = v;
        }
        else {
            *outU = p->anchorU;
            *outV = p->anchorV;
        }
        if (release) {
            p->down = 0;
            p->prevValid = !p->left;
            p->prevU = *outU;
            p->prevV = *outV;
            p->prevReleaseNs = nowNs;
        }
        return;
    }
    *outU = u;
    *outV = v;
}
