// The keyboard's sheets and its held modifiers. See xr_keys.h.
#include "xr_keys.h"

int kbModifierBit(int code) {
    switch (code) {
        case KB_CODE_CTRL:
            return KB_MOD_CTRL;
        case KB_CODE_ALT:
            return KB_MOD_ALT;
        case KB_CODE_WIN:
            return KB_MOD_WIN;
        default:
            return 0;
    }
}

// ---- the media keys row ----------------------------------------------------------------

static const int MEDIA_VK[KB_MEDIA_KEYS] = { 0xB1, 0xB3, 0xB0, 0xAD, 0xAE, 0xAF };

// Where the row sits, in rows of the panel: under the main block, with the
// gap the main block's rows have and the bottom margin they have
#define MEDIA_TOP_PX 438.0f
#define MEDIA_H_PX 60.0f
#define MEDIA_PAD_U 0.012f
#define MEDIA_GAP_U 0.005f

int kbMediaVk(int i) {
    return i >= 0 && i < KB_MEDIA_KEYS ? MEDIA_VK[i] : 0;
}

int kbMediaCode(int i) {
    return i >= 0 && i < KB_MEDIA_KEYS ? KB_CODE_VK + MEDIA_VK[i] : 0;
}

int kbMediaIndex(int code) {
    for (int i = 0; i < KB_MEDIA_KEYS; i++) {
        if (code == KB_CODE_VK + MEDIA_VK[i]) {
            return i;
        }
    }
    return -1;
}

void kbMediaRect(int i, float r[4]) {
    if (i < 0 || i >= KB_MEDIA_KEYS) {
        r[0] = r[1] = r[2] = r[3] = 0.0f;
        return;
    }
    float w = (1.0f - 2.0f * MEDIA_PAD_U) / (float)KB_MEDIA_KEYS;
    r[0] = MEDIA_PAD_U + (float)i * w + MEDIA_GAP_U * 0.5f;
    r[1] = MEDIA_TOP_PX / (float)KB_TEX_H;
    r[2] = MEDIA_PAD_U + (float)(i + 1) * w - MEDIA_GAP_U * 0.5f;
    r[3] = (MEDIA_TOP_PX + MEDIA_H_PX) / (float)KB_TEX_H;
}

float kbMainV(float v) {
    return v * (float)KB_MAIN_TEX_H / (float)KB_TEX_H;
}

// ---- sticky modifiers ------------------------------------------------------------------------

void kbStickyInit(KbSticky* s) {
    s->locked = 0;
    s->holdBit = 0;
    s->holdStartNs = 0;
}

int kbStickyBit(int code) {
    switch (code) {
        case KB_CODE_CTRL:
            return KB_MOD_CTRL;
        case KB_CODE_ALT:
            return KB_MOD_ALT;
        case KB_CODE_WIN:
            return KB_MOD_WIN;
        case KB_CODE_SHIFT:
            return KB_STICKY_SHIFT;
        default:
            return 0;
    }
}

int kbStickyPress(KbSticky* s, int code, long long nowNs) {
    int bit = kbStickyBit(code);
    s->holdBit = 0;
    if (bit == 0) {
        return 0;
    }
    if (s->locked & bit) {
        s->locked &= ~bit;
        return 1;
    }
    s->holdBit = bit;
    s->holdStartNs = nowNs;
    return 0;
}

int kbStickyTick(KbSticky* s, int down, long long nowNs, long long holdNs) {
    if (s->holdBit == 0) {
        return 0;
    }
    if (!down) {
        s->holdBit = 0;
        return 0;
    }
    if (nowNs - s->holdStartNs < holdNs) {
        return 0;
    }
    int bit = s->holdBit;
    s->locked |= bit;
    s->holdBit = 0;
    return bit;
}

int kbStickyMods(const KbSticky* s, int mods) {
    return mods | (s->locked & ~KB_STICKY_SHIFT);
}

int kbStickySheet(const KbSticky* s, int sheet) {
    if ((s->locked & KB_STICKY_SHIFT) && sheet == KB_STATE_LOWER) {
        return KB_STATE_UPPER;
    }
    return sheet;
}

KbPress kbPress(int sheet, int mods, int code) {
    KbPress p;
    p.sheet = sheet;
    p.mods = mods;
    p.code = -1;
    p.codeMods = 0;
    p.hide = 0;

    int bit = kbModifierBit(code);
    if (bit != 0) {
        p.mods = mods ^ bit;
    }
    else if (code == KB_CODE_SHIFT) {
        // Shift off the symbols page goes to the capitals rather than back
        // where it came from
        p.sheet = sheet == KB_STATE_UPPER ? KB_STATE_LOWER : KB_STATE_UPPER;
    }
    else if (code == KB_CODE_SYMBOLS) {
        // The ABC key on the symbols and the Fn sheet, ?123 on the letters
        p.sheet = sheet == KB_STATE_SYMBOLS || sheet == KB_STATE_FN ? KB_STATE_LOWER
                                                                    : KB_STATE_SYMBOLS;
    }
    else if (code == KB_CODE_FN) {
        // Fn on the symbols, and ?123 in the same place on the Fn sheet
        p.sheet = sheet == KB_STATE_FN ? KB_STATE_SYMBOLS : KB_STATE_FN;
    }
    else if (code == KB_CODE_HIDE) {
        p.hide = 1;
        p.mods = 0;
    }
    else if (code > 0) {
        p.code = code;
        p.codeMods = mods;
        p.mods = 0;
        // Shift is one shot over the letters, the way a phone keyboard
        // behaves, and sticky over the punctuation row above them
        if (sheet == KB_STATE_UPPER && ((code >= 'A' && code <= 'Z') || code >= KB_CODE_SVKL)) {
            p.sheet = KB_STATE_LOWER;
        }
    }
    return p;
}
