// Mouse emulation on a physical gamepad, see xr_padmouse.h. GPLv3.
#include "xr_padmouse.h"

#include <math.h>
#include <string.h>

// Only the bits of the pad that are buttons (a physical pad's packet also
// carries paddles, the touchpad click and misc above these)
#define BUTTON_MASK 0xffff

// What each pad button does while it is a mouse. A mouse button's down state
// is bit i of downMask, a key's is bit KEY_BASE + i.
#define KEY_BASE 5

static const struct { int pad; int button; } MOUSE_MAP[] = {
    { PAD_A, PADMOUSE_BTN_LEFT },
    { PAD_B, PADMOUSE_BTN_RIGHT },
    { PAD_X, PADMOUSE_BTN_MIDDLE },
    { PAD_LB, PADMOUSE_BTN_X1 },
    { PAD_RB, PADMOUSE_BTN_X2 },
};
#define MOUSE_COUNT ((int)(sizeof(MOUSE_MAP) / sizeof(MOUSE_MAP[0])))

static const struct { int pad; int vk; } KEY_MAP[] = {
    { PAD_UP, PADMOUSE_VK_UP },
    { PAD_DOWN, PADMOUSE_VK_DOWN },
    { PAD_LEFT, PADMOUSE_VK_LEFT },
    { PAD_RIGHT, PADMOUSE_VK_RIGHT },
};
#define KEY_COUNT ((int)(sizeof(KEY_MAP) / sizeof(KEY_MAP[0])))

int padMouseChordClamp(int chord) {
    return chord < PAD_MOUSE_CHORD_START_HOLD || chord > PAD_MOUSE_CHORD_OFF ? PAD_MOUSE_CHORD_START_HOLD
                                                                              : chord;
}

int padMouseScrollClamp(int scroll) {
    return scroll < PAD_MOUSE_SCROLL_RIGHT || scroll > PAD_MOUSE_SCROLL_NONE ? PAD_MOUSE_SCROLL_RIGHT : scroll;
}

int padArbitrationClamp(int mode) {
    return mode < PAD_ARB_POINTER || mode > PAD_ARB_SLEEP ? PAD_ARB_POINTER : mode;
}

void padMouseInit(PadMouse* pm) {
    memset(pm, 0, sizeof(*pm));
    pm->chord = PAD_MOUSE_CHORD_START_HOLD;
    pm->scroll = PAD_MOUSE_SCROLL_RIGHT;
}

void padMouseConfigure(PadMouse* pm, int chord, int scroll) {
    pm->chord = padMouseChordClamp(chord);
    pm->scroll = padMouseScrollClamp(scroll);
}

// The pad buttons that make the chord, 0 when there is none
static int chordMask(int chord) {
    switch (chord) {
    case PAD_MOUSE_CHORD_START_HOLD: return PAD_START;
    case PAD_MOUSE_CHORD_START_BACK: return PAD_START | PAD_BACK;
    default: return 0;
    }
}

float padMouseTravel(float deflection) {
    if (!(deflection > PADMOUSE_DEADZONE)) return 0.0f;
    float t = (deflection - PADMOUSE_DEADZONE) / (1.0f - PADMOUSE_DEADZONE);
    return t > 1.0f ? 1.0f : t;
}

float padMouseSpeed(float deflection) {
    float t = padMouseTravel(deflection);
    return PADMOUSE_SPEED_LINEAR * t + PADMOUSE_SPEED_SQUARE * t * t;
}

float padMouseScrollSpeed(float deflection) {
    float t = padMouseTravel(deflection);
    return PADMOUSE_SCROLL_FULL * t * t;
}

const char* padMouseNoticeText(int on, int chord) {
    if (!on) return "Gamepad is a gamepad again";
    switch (chord) {
    case PAD_MOUSE_CHORD_START_HOLD: return "Gamepad is a mouse - hold Start to give it back";
    case PAD_MOUSE_CHORD_START_BACK: return "Gamepad is a mouse - hold Start and Back to give it back";
    default: return "Gamepad is a mouse";
    }
}

// ---- what the host has down ----------------------------------------------

static void setMouse(PadMouse* pm, int i, int down, const PadMouseSink* sink) {
    int bit = 1 << i;
    if (((pm->downMask & bit) != 0) == (down != 0)) return;
    pm->downMask = down ? (pm->downMask | bit) : (pm->downMask & ~bit);
    if (sink && sink->button) sink->button(sink->user, MOUSE_MAP[i].button, down);
}

static void setKey(PadMouse* pm, int i, int down, const PadMouseSink* sink) {
    int bit = 1 << (KEY_BASE + i);
    if (((pm->downMask & bit) != 0) == (down != 0)) return;
    pm->downMask = down ? (pm->downMask | bit) : (pm->downMask & ~bit);
    if (sink && sink->key) sink->key(sink->user, KEY_MAP[i].vk, down);
}

static void letGo(PadMouse* pm, const PadMouseSink* sink) {
    for (int i = 0; i < MOUSE_COUNT; i++) setMouse(pm, i, 0, sink);
    for (int i = 0; i < KEY_COUNT; i++) setKey(pm, i, 0, sink);
    pm->remX = pm->remY = pm->remScroll = pm->remHScroll = 0.0f;
}

void padMouseRelease(PadMouse* pm, const PadMouseSink* sink) {
    letGo(pm, sink);
    pm->on = 0;
    pm->holding = 0;
    pm->strip = 0;
    pm->prevButtons = 0;
}

int padMouseWantsTick(const PadMouse* pm) { return pm->on || pm->holding; }

// ---- the emulation -----------------------------------------------------------

static float axisOf(int v) {
    float f = (float)v / 32767.0f;
    return f > 1.0f ? 1.0f : (f < -1.0f ? -1.0f : f);
}

// Whole units out of an accumulated amount, the remainder kept
static int takeWhole(float* acc) {
    int whole = (int)*acc; // toward zero
    *acc -= (float)whole;
    return whole;
}

static void emulate(PadMouse* pm, const PadMouseIn* in, int held, int64_t nowNs, const PadMouseSink* sink) {
    int64_t dtNs = nowNs - pm->lastNs;
    if (dtNs < 0) dtNs = 0;
    if (dtNs > PADMOUSE_DT_MAX_NS) dtNs = PADMOUSE_DT_MAX_NS;
    pm->lastNs = nowNs;
    float dt = (float)dtNs / 1e9f;

    // The buttons and keys, from the ones not held since the mode flipped
    for (int i = 0; i < MOUSE_COUNT; i++) setMouse(pm, i, (held & MOUSE_MAP[i].pad) != 0, sink);
    for (int i = 0; i < KEY_COUNT; i++) setKey(pm, i, (held & KEY_MAP[i].pad) != 0, sink);
    if ((held & PAD_Y) && !(pm->prevButtons & PAD_Y) && sink && sink->keyboard) sink->keyboard(sink->user);

    // Which stick points and which scrolls
    int px = in->leftX, py = in->leftY, sx = in->rightX, sy = in->rightY;
    int scrolls = pm->scroll != PAD_MOUSE_SCROLL_NONE;
    if (pm->scroll == PAD_MOUSE_SCROLL_LEFT) {
        px = in->rightX;
        py = in->rightY;
        sx = in->leftX;
        sy = in->leftY;
    }

    // The pointer: direction of the stick, speed from its curve, y up on the stick is up on the screen
    float fx = axisOf(px), fy = axisOf(py);
    float mag = sqrtf(fx * fx + fy * fy);
    float speed = padMouseSpeed(mag);
    if (speed > 0.0f && mag > 0.0f) {
        pm->remX += fx / mag * speed * dt;
        pm->remY += -fy / mag * speed * dt;
    } else {
        pm->remX = pm->remY = 0.0f;
    }
    int dx = takeWhole(&pm->remX), dy = takeWhole(&pm->remY);
    if ((dx || dy) && sink && sink->move) sink->move(sink->user, dx, dy);

    // The wheel: whichever way the scroll stick leans further, so a stick that is
    // pushed up does not wander sideways
    if (!scrolls) return;
    float gx = axisOf(sx), gy = axisOf(sy);
    float ax = fabsf(gx), ay = fabsf(gy);
    if (ay >= ax && padMouseScrollSpeed(ay) > 0.0f) {
        pm->remScroll += (gy < 0 ? -1.0f : 1.0f) * padMouseScrollSpeed(ay) * dt;
        pm->remHScroll = 0.0f;
    } else if (ax > ay && padMouseScrollSpeed(ax) > 0.0f) {
        pm->remHScroll += (gx < 0 ? -1.0f : 1.0f) * padMouseScrollSpeed(ax) * dt;
        pm->remScroll = 0.0f;
    } else {
        pm->remScroll = pm->remHScroll = 0.0f;
    }
    int v = takeWhole(&pm->remScroll), h = takeWhole(&pm->remHScroll);
    if (v && sink && sink->scroll) sink->scroll(sink->user, v);
    if (h && sink && sink->hscroll) sink->hscroll(sink->user, h);
}

static void flip(PadMouse* pm, int buttons, int64_t nowNs, const PadMouseSink* sink) {
    if (pm->on) {
        letGo(pm, sink);
        pm->on = 0;
    } else {
        pm->on = 1;
        pm->lastNs = nowNs;
        pm->prevButtons = buttons;
    }
    // What is held now (the chord) belongs to neither the game nor the mouse until let go
    pm->strip = buttons;
    pm->holding = 0;
    if (sink && sink->mode) sink->mode(sink->user, pm->on);
}

PadMouseOut padMouseStep(PadMouse* pm, const PadMouseIn* in, int live, int64_t nowNs,
                         const PadMouseSink* sink) {
    PadMouseOut out = { 0, 0 };
    int buttons = in->buttons & BUTTON_MASK;
    if (!live) {
        padMouseRelease(pm, sink);
        pm->prevButtons = buttons;
        return out;
    }

    int mask = chordMask(pm->chord);
    pm->strip &= buttons; // a button let go is the game's (or the mouse's) again
    if (mask == 0) {
        // The chord was switched off: a mode that is on ends, nothing starts one
        pm->holding = 0;
        if (pm->on) flip(pm, buttons, nowNs, sink);
    } else if (pm->strip == 0 && buttons == mask) {
        if (!pm->holding) {
            pm->holding = 1;
            pm->holdStartNs = nowNs;
        } else if (nowNs - pm->holdStartNs >= PADMOUSE_HOLD_NS) {
            flip(pm, buttons, nowNs, sink);
        }
    } else {
        pm->holding = 0;
    }

    if (pm->on) {
        emulate(pm, in, buttons & ~pm->strip, nowNs, sink);
        out.neutral = 1;
    } else {
        out.strip = pm->strip;
    }
    pm->prevButtons = buttons & ~pm->strip;
    return out;
}
