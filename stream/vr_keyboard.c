// Moonlight XR Native: VrKeyboard.java in C (see vr_keyboard.h). GPLv3.
#include "vr_keyboard.h"

#include <Limelight.h>
#include <stdio.h>

#include "xr_kbfull.h"
#include "xr_shared.h"

#define VK_SHIFT 0x10

// In the order they go down, and come back up the other way
#define MOD_COUNT 8
static const int MOD_BITS[MOD_COUNT] = { KBF_LSHIFT, KBF_LCTRL, KBF_LALT, KBF_LWIN,
                                         KBF_RSHIFT, KBF_RCTRL, KBF_RALT, KBF_RWIN };
static const short MOD_KEYS[MOD_COUNT] = { 0xA0, 0xA2, 0xA4, 0x5B, 0xA1, 0xA3, 0xA5, 0x5C };

// The KBF_ bits as moonlight-common-c's modifier byte
static char modifierByte(int mods) {
    char m = 0;
    int b = kbfModifierByte(mods);
    if (b & 0x01) m |= MODIFIER_SHIFT;
    if (b & 0x02) m |= MODIFIER_CTRL;
    if (b & 0x04) m |= MODIFIER_ALT;
    if (b & 0x08) m |= MODIFIER_META;
    return m;
}

void vrkb_init(VrKeyboard* k, VrKeyboardSink sink) {
    k->sink = sink;
    k->held = 0;
    k->heldKey = 0;
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
    mods &= KBF_MOD_MASK;
    for (int i = 0; i < MOD_COUNT; i++) {
        int bit = MOD_BITS[i];
        if ((mods & bit) && !(k->held & bit)) {
            k->held |= bit;
            k->sink.key(k->sink.user, MOD_KEYS[i], KEY_ACTION_DOWN, modifierByte(k->held));
        }
    }
    for (int i = MOD_COUNT - 1; i >= 0; i--) {
        int bit = MOD_BITS[i];
        if (!(mods & bit) && (k->held & bit)) {
            k->held &= ~bit;
            k->sink.key(k->sink.user, MOD_KEYS[i], KEY_ACTION_UP, modifierByte(k->held));
        }
    }
}

void vrkb_release_all(VrKeyboard* k) {
    if (k->heldKey != 0) {
        k->sink.key(k->sink.user, (short)k->heldKey, KEY_ACTION_UP, modifierByte(k->held));
        k->heldKey = 0;
    }
    vrkb_hold(k, 0);
}

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
    int flags = mods & (KBF_FLAG_DOWN | KBF_FLAG_UP);
    mods &= KBF_MOD_MASK;
    if (flags != 0 && code >= KB_CODE_VK) {
        // The Full keyboard: one event of a key held like a real one
        short vk = (short)(code - KB_CODE_VK);
        if (flags & KBF_FLAG_UP) {
            k->sink.key(k->sink.user, vk, KEY_ACTION_UP, modifierByte(mods));
            if (k->heldKey == vk) k->heldKey = 0;
        } else {
            vrkb_hold(k, k->held | mods);
            // A key repeating is another down; a different key lets the old one go
            if (k->heldKey != 0 && k->heldKey != vk) {
                k->sink.key(k->sink.user, (short)k->heldKey, KEY_ACTION_UP, modifierByte(mods));
            }
            k->sink.key(k->sink.user, vk, KEY_ACTION_DOWN, modifierByte(mods));
            k->heldKey = vk;
        }
        return;
    }
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
