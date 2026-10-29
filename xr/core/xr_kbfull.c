// The Full keyboard's layout and rules. See xr_kbfull.h. GPLv3.
#include "xr_kbfull.h"

#include <string.h>

#define PAD_U 0.012f
#define PAD_V 0.030f
#define GAP_U 0.004f
#define GAP_V 0.012f

// label, shifted, vk, ext, kind, modBit, letter, x, y, w, h
#define N(l, s, vk, ext, x, y, w) { l, s, vk, ext, KBF_NORMAL, 0, 0, x, y, w, 1.0f }
#define L(l, s, vk, x, y) { l, s, vk, 0, KBF_NORMAL, 0, 1, x, y, 1.0f, 1.0f }
#define M(l, vk, ext, bit, x, y, w) { l, NULL, vk, ext, KBF_MOD, bit, 0, x, y, w, 1.0f }

// Row y: 0 for the function row, then 1.5 + row (a half key of space under it)
#define R1 1.5f
#define R2 2.5f
#define R3 3.5f
#define R4 4.5f
#define R5 5.5f

static const KbfKey KEYS[KBF_KEY_COUNT] = {
    // ---- row 0: Esc, F1-F12 in fours, PrtSc ScrLk Pause
    N("Esc", NULL, 0x1B, 0, 0.0f, 0, 1.0f),
    N("F1", NULL, 0x70, 0, 2.0f, 0, 1.0f), N("F2", NULL, 0x71, 0, 3.0f, 0, 1.0f),
    N("F3", NULL, 0x72, 0, 4.0f, 0, 1.0f), N("F4", NULL, 0x73, 0, 5.0f, 0, 1.0f),
    N("F5", NULL, 0x74, 0, 6.5f, 0, 1.0f), N("F6", NULL, 0x75, 0, 7.5f, 0, 1.0f),
    N("F7", NULL, 0x76, 0, 8.5f, 0, 1.0f), N("F8", NULL, 0x77, 0, 9.5f, 0, 1.0f),
    N("F9", NULL, 0x78, 0, 11.0f, 0, 1.0f), N("F10", NULL, 0x79, 0, 12.0f, 0, 1.0f),
    N("F11", NULL, 0x7A, 0, 13.0f, 0, 1.0f), N("F12", NULL, 0x7B, 0, 14.0f, 0, 1.0f),
    N("PrtSc", NULL, 0x2C, 1, 15.25f, 0, 1.0f), N("ScrLk", NULL, 0x91, 0, 16.25f, 0, 1.0f),
    N("Pause", NULL, 0x13, 0, 17.25f, 0, 1.0f),
    // ---- row 1: numbers, Backspace, Ins Home PgUp
    N("`", "~", 0xC0, 0, 0.0f, R1, 1.0f), N("1", "!", 0x31, 0, 1.0f, R1, 1.0f),
    N("2", "@", 0x32, 0, 2.0f, R1, 1.0f), N("3", "#", 0x33, 0, 3.0f, R1, 1.0f),
    N("4", "$", 0x34, 0, 4.0f, R1, 1.0f), N("5", "%", 0x35, 0, 5.0f, R1, 1.0f),
    N("6", "^", 0x36, 0, 6.0f, R1, 1.0f), N("7", "&", 0x37, 0, 7.0f, R1, 1.0f),
    N("8", "*", 0x38, 0, 8.0f, R1, 1.0f), N("9", "(", 0x39, 0, 9.0f, R1, 1.0f),
    N("0", ")", 0x30, 0, 10.0f, R1, 1.0f), N("-", "_", 0xBD, 0, 11.0f, R1, 1.0f),
    N("=", "+", 0xBB, 0, 12.0f, R1, 1.0f), N("Bksp", NULL, 0x08, 0, 13.0f, R1, 2.0f),
    N("Ins", NULL, 0x2D, 1, 15.25f, R1, 1.0f), N("Home", NULL, 0x24, 1, 16.25f, R1, 1.0f),
    N("PgUp", NULL, 0x21, 1, 17.25f, R1, 1.0f),
    // ---- row 2: Tab, QWERTY, Del End PgDn
    N("Tab", NULL, 0x09, 0, 0.0f, R2, 1.5f),
    L("q", "Q", 0x51, 1.5f, R2), L("w", "W", 0x57, 2.5f, R2), L("e", "E", 0x45, 3.5f, R2),
    L("r", "R", 0x52, 4.5f, R2), L("t", "T", 0x54, 5.5f, R2), L("y", "Y", 0x59, 6.5f, R2),
    L("u", "U", 0x55, 7.5f, R2), L("i", "I", 0x49, 8.5f, R2), L("o", "O", 0x4F, 9.5f, R2),
    L("p", "P", 0x50, 10.5f, R2),
    N("[", "{", 0xDB, 0, 11.5f, R2, 1.0f), N("]", "}", 0xDD, 0, 12.5f, R2, 1.0f),
    N("\\", "|", 0xDC, 0, 13.5f, R2, 1.5f),
    N("Del", NULL, 0x2E, 1, 15.25f, R2, 1.0f), N("End", NULL, 0x23, 1, 16.25f, R2, 1.0f),
    N("PgDn", NULL, 0x22, 1, 17.25f, R2, 1.0f),
    // ---- row 3: Caps, ASDF, Enter; the layout key where the numpad would end
    { "Caps", NULL, 0x14, 0, KBF_CAPS, 0, 0, 0.0f, R3, 1.75f, 1.0f },
    L("a", "A", 0x41, 1.75f, R3), L("s", "S", 0x53, 2.75f, R3), L("d", "D", 0x44, 3.75f, R3),
    L("f", "F", 0x46, 4.75f, R3), L("g", "G", 0x47, 5.75f, R3), L("h", "H", 0x48, 6.75f, R3),
    L("j", "J", 0x4A, 7.75f, R3), L("k", "K", 0x4B, 8.75f, R3), L("l", "L", 0x4C, 9.75f, R3),
    N(";", ":", 0xBA, 0, 10.75f, R3, 1.0f), N("'", "\"", 0xDE, 0, 11.75f, R3, 1.0f),
    N("Enter", NULL, 0x0D, 0, 12.75f, R3, 2.25f),
    { "@layout", NULL, 0, 0, KBF_LAYOUT, 0, 0, 15.25f, R3, 3.0f, 1.0f },
    // ---- row 4: Shift, ZXCV, Shift, Up
    M("@shift", 0xA0, 0, KBF_LSHIFT, 0.0f, R4, 2.25f),
    L("z", "Z", 0x5A, 2.25f, R4), L("x", "X", 0x58, 3.25f, R4), L("c", "C", 0x43, 4.25f, R4),
    L("v", "V", 0x56, 5.25f, R4), L("b", "B", 0x42, 6.25f, R4), L("n", "N", 0x4E, 7.25f, R4),
    L("m", "M", 0x4D, 8.25f, R4),
    N(",", "<", 0xBC, 0, 9.25f, R4, 1.0f), N(".", ">", 0xBE, 0, 10.25f, R4, 1.0f),
    N("/", "?", 0xBF, 0, 11.25f, R4, 1.0f),
    M("@shift", 0xA1, 0, KBF_RSHIFT, 12.25f, R4, 2.75f),
    N("@up", NULL, 0x26, 1, 16.25f, R4, 1.0f),
    // ---- row 5: Ctrl Win Alt Space Alt Win Menu Ctrl, Left Down Right
    M("@ctrl", 0xA2, 0, KBF_LCTRL, 0.0f, R5, 1.25f),
    M("@win", 0x5B, 1, KBF_LWIN, 1.25f, R5, 1.25f),
    M("@alt", 0xA4, 0, KBF_LALT, 2.5f, R5, 1.25f),
    N("@space", NULL, 0x20, 0, 3.75f, R5, 6.25f),
    M("@alt", 0xA5, 1, KBF_RALT, 10.0f, R5, 1.25f),
    M("@win", 0x5C, 1, KBF_RWIN, 11.25f, R5, 1.25f),
    N("Menu", NULL, 0x5D, 1, 12.5f, R5, 1.25f),
    M("@ctrl", 0xA3, 1, KBF_RCTRL, 13.75f, R5, 1.25f),
    N("@left", NULL, 0x25, 1, 15.25f, R5, 1.0f), N("@down", NULL, 0x28, 1, 16.25f, R5, 1.0f),
    N("@right", NULL, 0x27, 1, 17.25f, R5, 1.0f),
};

// The key the French and German boards have between the left Shift and Z
static const KbfKey ISO_KEY = N("<", ">", 0xE2, 0, 1.25f, R4, 1.0f);
#define ISO_SHIFT_W 1.25f

// What a map changes about a key: its labels (the virtual key stays). A
// digit key is a "letter" on AZERTY, where Caps Lock gives the digits; Caps
// does not touch the keys that make punctuation.
typedef struct {
    int vk;
    const char* label;
    const char* shifted;  // NULL: the key has no second symbol
    int letter;
} MapKey;

static const MapKey AZERTY_KEYS[] = {
    { 0xC0, "²", NULL, 0 },
    { 0x31, "&", "1", 1 }, { 0x32, "é", "2", 1 }, { 0x33, "\"", "3", 1 },
    { 0x34, "'", "4", 1 }, { 0x35, "(", "5", 1 }, { 0x36, "-", "6", 1 },
    { 0x37, "è", "7", 1 }, { 0x38, "_", "8", 1 }, { 0x39, "ç", "9", 1 },
    { 0x30, "à", "0", 1 },
    { 0xBD, ")", "°", 0 }, { 0xBB, "=", "+", 0 },
    { 0x51, "a", "A", 1 }, { 0x57, "z", "Z", 1 },
    { 0xDB, "^", "¨", 0 }, { 0xDD, "$", "£", 0 }, { 0xDC, "*", "µ", 0 },
    { 0x41, "q", "Q", 1 }, { 0xBA, "m", "M", 1 }, { 0xDE, "ù", "%", 0 },
    { 0x5A, "w", "W", 1 }, { 0x4D, ",", "?", 0 }, { 0xBC, ";", ".", 0 },
    { 0xBE, ":", "/", 0 }, { 0xBF, "!", "§", 0 },
};

static const MapKey QWERTZ_KEYS[] = {
    { 0xC0, "^", "°", 0 },
    { 0x31, "1", "!", 0 }, { 0x32, "2", "\"", 0 }, { 0x33, "3", "§", 0 },
    { 0x34, "4", "$", 0 }, { 0x35, "5", "%", 0 }, { 0x36, "6", "&", 0 },
    { 0x37, "7", "/", 0 }, { 0x38, "8", "(", 0 }, { 0x39, "9", ")", 0 },
    { 0x30, "0", "=", 0 },
    { 0xBD, "ß", "?", 0 }, { 0xBB, "´", "`", 0 },
    { 0x59, "z", "Z", 1 },
    { 0xDB, "ü", "Ü", 1 }, { 0xDD, "+", "*", 0 }, { 0xDC, "#", "'", 0 },
    { 0xBA, "ö", "Ö", 1 }, { 0xDE, "ä", "Ä", 1 },
    { 0x5A, "y", "Y", 1 }, { 0xBC, ",", ";", 0 }, { 0xBE, ".", ":", 0 }, { 0xBF, "-", "_", 0 },
};

static const MapKey* mapKey(int map, int vk) {
    const MapKey* t = NULL;
    int n = 0;
    if (map == KB_MAP_AZERTY) {
        t = AZERTY_KEYS;
        n = (int)(sizeof(AZERTY_KEYS) / sizeof(AZERTY_KEYS[0]));
    } else if (map == KB_MAP_QWERTZ) {
        t = QWERTZ_KEYS;
        n = (int)(sizeof(QWERTZ_KEYS) / sizeof(QWERTZ_KEYS[0]));
    }
    for (int i = 0; i < n; i++) {
        if (t[i].vk == vk) return &t[i];
    }
    return NULL;
}

static int isoMap(int map) { return map == KB_MAP_AZERTY || map == KB_MAP_QWERTZ; }

const KbfKey* kbfKey(int index) {
    if (index == KBF_ISO_KEY) return &ISO_KEY;
    return index >= 0 && index < KBF_KEY_COUNT ? &KEYS[index] : NULL;
}

int kbfKeyCountFor(int map) { return isoMap(map) ? KBF_KEY_COUNT_MAX : KBF_KEY_COUNT; }

void kbfKeyRectFor(int map, int index, float r[4]) {
    const KbfKey* k = kbfKey(index);
    if (!k || (index == KBF_ISO_KEY && !isoMap(map))) {
        r[0] = r[1] = r[2] = r[3] = 0.0f;
        return;
    }
    float w = k->w;
    if (isoMap(map) && k->kind == KBF_MOD && k->modBit == KBF_LSHIFT) w = ISO_SHIFT_W;
    float uw = (1.0f - 2.0f * PAD_U) / KBF_UNITS_W;
    float uh = (1.0f - 2.0f * PAD_V) / KBF_UNITS_H;
    r[0] = PAD_U + k->x * uw + GAP_U * 0.5f;
    r[1] = PAD_V + k->y * uh + GAP_V * 0.5f;
    r[2] = PAD_U + (k->x + w) * uw - GAP_U * 0.5f;
    r[3] = PAD_V + (k->y + k->h) * uh - GAP_V * 0.5f;
}

void kbfKeyRect(int index, float r[4]) { kbfKeyRectFor(KB_MAP_QWERTY, index, r); }

int kbfKeyCode(int index) {
    const KbfKey* k = kbfKey(index);
    if (!k) return 0;
    return k->kind == KBF_LAYOUT ? KB_CODE_LAYOUT : KB_CODE_VK + k->vk;
}

int kbfFindVk(int vk) {
    for (int i = 0; i < KBF_KEY_COUNT; i++) {
        if (KEYS[i].vk == vk && KEYS[i].kind != KBF_LAYOUT) return i;
    }
    return -1;
}

const char* kbfLabelFor(int map, int index, int mods, int caps) {
    const KbfKey* k = kbfKey(index);
    if (!k) return NULL;
    const char* label = k->label;
    const char* shifted = k->shifted;
    int letter = k->letter;
    const MapKey* m = k->kind == KBF_NORMAL ? mapKey(map, k->vk) : NULL;
    if (m) {
        label = m->label;
        shifted = m->shifted;
        letter = m->letter;
    }
    if (!shifted) return label;
    int shift = (mods & KBF_SHIFTS) != 0;
    int up = letter ? (shift != (caps != 0)) : shift;
    return up ? shifted : label;
}

const char* kbfLabel(int index, int mods, int caps) {
    return kbfLabelFor(KB_MAP_QWERTY, index, mods, caps);
}

int kbfModifierByte(int mods) {
    int m = 0;
    if (mods & KBF_SHIFTS) m |= 0x01;
    if (mods & (KBF_LCTRL | KBF_RCTRL)) m |= 0x02;
    if (mods & (KBF_LALT | KBF_RALT)) m |= 0x04;
    if (mods & (KBF_LWIN | KBF_RWIN)) m |= 0x08;
    return m;
}

// ---- the rules --------------------------------------------------------------

void kbfInit(KbFull* k) {
    memset(k, 0, sizeof(*k));
    k->held = -1;
    k->hand = -1;
    k->holdNs = KBF_HOLD_NS_DEFAULT;
}

void kbfSetHoldMs(KbFull* k, int ms) {
    if (ms < 100) ms = 100;
    if (ms > 3000) ms = 3000;
    k->holdNs = (int64_t)ms * 1000000LL;
}

int kbfDownMods(const KbFull* k) { return k->oneshot | k->locked; }

static void fill(const KbFull* k, KbfEvent* ev, int kind, const KbfKey* key) {
    ev->kind = kind;
    ev->vk = key ? key->vk : 0;
    ev->ext = key ? key->ext : 0;
    ev->mods = kbfDownMods(k);
}

void kbfPress(KbFull* k, int key, int hand, int64_t now, KbfEvent* ev) {
    ev->kind = KBF_EV_NONE;
    ev->vk = ev->ext = ev->mods = 0;
    const KbfKey* kk = kbfKey(key);
    if (!kk || k->held >= 0) return;
    if (kk->kind == KBF_LAYOUT) {
        fill(k, ev, KBF_EV_LAYOUT, kk);
        return;
    }
    k->held = key;
    k->hand = hand;
    k->pressNs = now;
    k->longDone = 0;
    k->nextRepeatNs = 0;
    if (kk->kind == KBF_MOD) {
        int bit = kk->modBit;
        if (k->locked & bit) {
            k->wasState = 2;
        } else if (k->oneshot & bit) {
            k->wasState = 1;
        } else {
            // Down on the host at the tap, as Hacker's Keyboard has it
            k->wasState = 0;
            k->oneshot |= bit;
        }
        return;
    }
    if (kk->kind == KBF_CAPS) k->caps = !k->caps;
    else k->nextRepeatNs = now + KBF_REPEAT_DELAY_NS;
    fill(k, ev, KBF_EV_DOWN, kk);
}

void kbfRelease(KbFull* k, int64_t now, KbfEvent* ev) {
    (void)now;
    ev->kind = KBF_EV_NONE;
    ev->vk = ev->ext = ev->mods = 0;
    if (k->held < 0) return;
    const KbfKey* kk = kbfKey(k->held);
    k->held = -1;
    k->hand = -1;
    k->nextRepeatNs = 0;
    if (kk->kind == KBF_MOD) {
        // A short press on something already lit puts it out; the hold has
        // locked it or not by now
        if (!k->longDone && k->wasState != 0) {
            k->oneshot &= ~kk->modBit;
            k->locked &= ~kk->modBit;
        }
        return;
    }
    fill(k, ev, KBF_EV_UP, kk);
    // The one-shot modifiers were for this key: they come up after it
    k->oneshot = 0;
}

void kbfTick(KbFull* k, int64_t now, KbfEvent* ev) {
    ev->kind = KBF_EV_NONE;
    ev->vk = ev->ext = ev->mods = 0;
    if (k->held < 0) return;
    const KbfKey* kk = kbfKey(k->held);
    if (kk->kind == KBF_MOD) {
        if (!k->longDone && now - k->pressNs >= k->holdNs) {
            k->longDone = 1;
            k->oneshot &= ~kk->modBit;
            k->locked |= kk->modBit;
        }
        return;
    }
    if (kk->kind == KBF_NORMAL && k->nextRepeatNs != 0 && now >= k->nextRepeatNs) {
        k->nextRepeatNs = now - k->nextRepeatNs > KBF_REPEAT_INTERVAL_NS
                ? now + KBF_REPEAT_INTERVAL_NS : k->nextRepeatNs + KBF_REPEAT_INTERVAL_NS;
        fill(k, ev, KBF_EV_DOWN, kk);
    }
}

void kbfReleaseAll(KbFull* k, KbfEvent* ev) {
    ev->kind = KBF_EV_NONE;
    ev->vk = ev->ext = ev->mods = 0;
    if (k->held >= 0) {
        const KbfKey* kk = kbfKey(k->held);
        if (kk->kind == KBF_NORMAL || kk->kind == KBF_CAPS) fill(k, ev, KBF_EV_UP, kk);
    }
    k->held = -1;
    k->hand = -1;
    k->nextRepeatNs = 0;
    k->oneshot = 0;
    k->locked = 0;
}
