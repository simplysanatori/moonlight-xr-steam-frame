// Moonlight XR Native: VrKeyboard.java in C (see vr_keyboard.h). GPLv3.
#include "vr_keyboard.h"

#include <Limelight.h>
#include <stdio.h>

#include "xr_shared.h"

#define VK_SHIFT 0x10
#define VK_LCONTROL 0xA2
#define VK_LMENU 0xA4
#define VK_LWIN 0x5B

// In the order they go down, and come back up the other way
static const int MOD_BITS[3] = { KB_MOD_CTRL, KB_MOD_ALT, KB_MOD_WIN };
static const short MOD_KEYS[3] = { VK_LCONTROL, VK_LMENU, VK_LWIN };

// The KB_MOD_ bits as moonlight-common-c's modifier byte
static char modifierByte(int mods) {
    char m = 0;
    if (mods & KB_MOD_CTRL) m |= MODIFIER_CTRL;
    if (mods & KB_MOD_ALT) m |= MODIFIER_ALT;
    if (mods & KB_MOD_WIN) m |= MODIFIER_META;
    return m;
}

void vrkb_init(VrKeyboard* k, VrKeyboardSink sink) {
    k->sink = sink;
    k->held = 0;
}

static void press(VrKeyboard* k, short keyCode, char modifiers) {
    k->sink.key(k->sink.user, keyCode, KEY_ACTION_DOWN, modifiers);
    k->sink.key(k->sink.user, keyCode, KEY_ACTION_UP, modifiers);
}

short vrkb_punctuation_key(int code) {
    switch (code) {
        case ';': return 0xBA;
        case '=': return 0xBB;
        case ',': return 0xBC;
        case '-': return 0xBD;
        case '.': return 0xBE;
        case '/': return 0xBF;
        case '`': return 0xC0;
        case '[': return 0xDB;
        case '\\': return 0xDC;
        case ']': return 0xDD;
        case '\'': return 0xDE;
        default: return 0;
    }
}

void vrkb_hold(VrKeyboard* k, int mods) {
    for (int i = 0; i < 3; i++) {
        int bit = MOD_BITS[i];
        if ((mods & bit) && !(k->held & bit)) {
            k->held |= bit;
            k->sink.key(k->sink.user, MOD_KEYS[i], KEY_ACTION_DOWN, modifierByte(k->held));
        }
    }
    for (int i = 2; i >= 0; i--) {
        int bit = MOD_BITS[i];
        if (!(mods & bit) && (k->held & bit)) {
            k->held &= ~bit;
            k->sink.key(k->sink.user, MOD_KEYS[i], KEY_ACTION_UP, modifierByte(k->held));
        }
    }
}

void vrkb_release_all(VrKeyboard* k) { vrkb_hold(k, 0); }

static int encodeUtf8(int cp, char out[5]) {
    int n = 0;
    if (cp < 0x80) {
        out[n++] = (char)cp;
    }
    else if (cp < 0x800) {
        out[n++] = (char)(0xC0 | (cp >> 6));
        out[n++] = (char)(0x80 | (cp & 0x3F));
    }
    else if (cp < 0x10000) {
        out[n++] = (char)(0xE0 | (cp >> 12));
        out[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[n++] = (char)(0x80 | (cp & 0x3F));
    }
    else {
        out[n++] = (char)(0xF0 | (cp >> 18));
        out[n++] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[n++] = (char)(0x80 | (cp & 0x3F));
    }
    out[n] = '\0';
    return n;
}

void vrkb_type(VrKeyboard* k, int code, int mods) {
    vrkb_hold(k, k->held | mods);
    char modifiers = modifierByte(mods);
    if (code >= KB_CODE_VK) {
        press(k, (short)(code - KB_CODE_VK), modifiers);
    }
    else if (code == 8 || code == 9 || code == 13 || code == 32 || (code >= '0' && code <= '9')) {
        press(k, (short)code, modifiers);
    }
    else if (code >= 'a' && code <= 'z') {
        press(k, (short)(code - 32), modifiers);
    }
    else if (code >= 'A' && code <= 'Z') {
        // Shift is held around the letter and named in the modifier as
        // well, so hosts that read either one see the capital
        k->sink.key(k->sink.user, VK_SHIFT, KEY_ACTION_DOWN, modifiers);
        press(k, (short)code, (char)(modifiers | MODIFIER_SHIFT));
        k->sink.key(k->sink.user, VK_SHIFT, KEY_ACTION_UP, modifiers);
    }
    else if (mods != 0 && vrkb_punctuation_key(code) != 0) {
        // Text cannot carry a modifier, so Ctrl and the minus key go as the
        // key itself
        press(k, vrkb_punctuation_key(code), modifiers);
    }
    else if (code > 0) {
        char utf8[5];
        encodeUtf8(code, utf8);
        k->sink.text(k->sink.user, utf8);
    }
}
