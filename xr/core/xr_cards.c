// See xr_cards.h. GPLv3.
#include "xr_cards.h"

#include <math.h>
#include <string.h>

void firstRunInit(FirstRun* f, int shown) {
    memset(f, 0, sizeof(*f));
    f->armed = shown ? 0 : 1;
}

int firstRunStep(FirstRun* f, int ready, int modal, int64_t nowNs) {
    if (f->showing) {
        if (modal || nowNs - f->startNs >= FIRSTRUN_SHOW_NS) {
            f->showing = 0;
            f->armed = 0;
        }
        return f->showing;
    }
    if (!f->armed || !ready) {
        return 0;
    }
    if (modal) {
        // already in a sheet: the card would only be in the way, and the controls are being learned
        f->armed = 0;
        return 0;
    }
    f->showing = 1;
    f->startNs = nowNs;
    f->markPending = 1;
    return 1;
}

int firstRunTakeMark(FirstRun* f) {
    int m = f->markPending;
    f->markPending = 0;
    return m;
}

int padStyleFor(int glyphSet, int known) {
    if (!known) {
        return PADSTYLE_GENERIC;
    }
    if (glyphSet >= PADSTYLE_XBOX && glyphSet <= PADSTYLE_STEAM) {
        return glyphSet;
    }
    return PADSTYLE_GENERIC;
}

int padCardVisible(int mode, int padActive, int64_t ageNs) {
    if (mode == PADCARD_OFF || !padActive) {
        return 0;
    }
    if (mode == PADCARD_ON) {
        return 1;
    }
    return ageNs < PADCARD_AUTOHIDE_NS;
}

float padStickMag(int x, int y) {
    float fx = (float)x / 32767.0f, fy = (float)y / 32767.0f;
    float m = sqrtf(fx * fx + fy * fy);
    return m > 1.0f ? 1.0f : m;
}

int padStickLit(int x, int y) {
    return padStickMag(x, y) >= PADLIVE_STICK_LIT;
}

int padTriggerLit(int v) {
    return v > PADLIVE_TRIGGER_LIT;
}

// v rounded to the nearest multiple of range/16 (symmetric about zero)
static int q16(int v, int range) {
    int step = range / 16;
    if (step < 1) {
        step = 1;
    }
    int r = v >= 0 ? (v + step / 2) / step : -((-v + step / 2) / step);
    return r * step;
}

void padLiveQuantise(const PadLive* in, PadLive* out) {
    *out = *in;
    out->lt = q16(in->lt, 256);
    out->rt = q16(in->rt, 256);
    out->lx = q16(in->lx, 32768);
    out->ly = q16(in->ly, 32768);
    out->rx = q16(in->rx, 32768);
    out->ry = q16(in->ry, 32768);
    out->touchX = floorf(in->touchX * 32.0f + 0.5f) / 32.0f;
    out->touchY = floorf(in->touchY * 32.0f + 0.5f) / 32.0f;
    if (!in->touching) {
        out->touchX = 0.0f;
        out->touchY = 0.0f;
    }
    // the card is drawn the same whenever the last input was
    out->ageNs = 0;
}

void padCardLap(float yawRad, float pos[3], float* tiltRad) {
    // forward along the heading: (-sin yaw, 0, -cos yaw)
    pos[0] = -sinf(yawRad) * PADCARD_FWD_M;
    pos[1] = -PADCARD_DOWN_M;
    pos[2] = -cosf(yawRad) * PADCARD_FWD_M;
    // the card's normal (+z) must point from the card to the eyes, up by DOWN and back by FWD;
    // rotating +z about +x by a gives (0, -sin a, cos a), so a = -atan2(DOWN, FWD)
    *tiltRad = -atan2f(PADCARD_DOWN_M, PADCARD_FWD_M);
}
