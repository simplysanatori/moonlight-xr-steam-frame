// Moonlight XR Native: VrKeyboard.java in C (see vr_keyboard.h). GPLv3.
#include "vr_keyboard.h"

#include <Limelight.h>
#include <stdio.h>
#include <string.h>

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

// A key with Shift held round it, as the host's layout reads the capital
// from the Shift and the key. Shift is named in the modifier as well, so
// hosts that read either one see it.
static void pressShifted(VrKeyboard* k, short vk, char modifiers) {
    k->sink.key(k->sink.user, VK_SHIFT, KEY_ACTION_DOWN, modifiers);
    press(k, vk, (char)(modifiers | MODIFIER_SHIFT));
    k->sink.key(k->sink.user, VK_SHIFT, KEY_ACTION_UP, modifiers);
}

// The bytes of the UTF-8 character at s (1 to 4) if all of it is there and well formed, else 0
static size_t utf8Length(const unsigned char* s, size_t left) {
    size_t n = s[0] < 0x80 ? 1 : (s[0] >> 5) == 0x6 ? 2 : (s[0] >> 4) == 0xE ? 3 : (s[0] >> 3) == 0x1E ? 4 : 0;
    if (n == 0 || n > left) {
        return 0;
    }
    for (size_t i = 1; i < n; i++) {
        if ((s[i] & 0xC0) != 0x80) {
            return 0;
        }
    }
    return n;
}

size_t vrkb_type_text(VrKeyboard* k, const char* utf8, size_t maxBytes) {
    char chunk[VRKB_TEXT_CHUNK + 1];
    size_t have = 0, used = 0;
    if (utf8 == NULL) {
        return 0;
    }
    size_t total = strlen(utf8);
    while (used < total && used < maxBytes) {
        const unsigned char* c = (const unsigned char*)utf8 + used;
        size_t left = total - used;
        size_t n = utf8Length(c, left);
        if (n == 0) {
            used++;      // not UTF-8: left out
            continue;
        }
        if (used + n > maxBytes) {
            break;       // the next character does not fit
        }
        if (n == 1 && (c[0] == '\n' || c[0] == '\r' || c[0] == '\t')) {
            if (have > 0) {
                chunk[have] = '\0';
                k->sink.text(k->sink.user, chunk);
                have = 0;
            }
            used++;
            // CR LF is one line end: the LF is the Enter key
            if (c[0] == '\r' && left > 1 && c[1] == '\n') {
                continue;
            }
            press(k, c[0] == '\t' ? 0x09 : 0x0D, 0);
            continue;
        }
        if (n == 1 && c[0] < 0x20) {
            used++;      // another control character
            continue;
        }
        if (have + n > VRKB_TEXT_CHUNK) {
            chunk[have] = '\0';
            k->sink.text(k->sink.user, chunk);
            have = 0;
        }
        memcpy(chunk + have, c, n);
        have += n;
        used += n;
    }
    if (have > 0) {
        chunk[have] = '\0';
        k->sink.text(k->sink.user, chunk);
    }
    return used;
}

void vrkb_type(VrKeyboard* k, int code, int mods) {
    int flags = mods & (KBF_FLAG_DOWN | KBF_FLAG_UP | KBF_FLAG_CHORD | KBF_FLAG_HOLD | KBF_FLAG_LET);
    mods &= KBF_MOD_MASK;
    // Native (the Host sheet and macros): HOLD and LET change the modifiers
    // down on the host and nothing else; CHORD taps a key with modifiers round
    // it and lets go of the ones it brought, so nothing is left held
    if (flags & KBF_FLAG_HOLD) {
        vrkb_hold(k, k->held | mods);
        return;
    }
    if (flags & KBF_FLAG_LET) {
        vrkb_hold(k, k->held & ~mods);
        return;
    }
    if ((flags & KBF_FLAG_CHORD) && code >= KB_CODE_VK) {
        int before = k->held;
        vrkb_hold(k, before | mods);
        press(k, (short)(code - KB_CODE_VK), modifierByte(k->held));
        vrkb_hold(k, before);
        return;
    }
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
    if (code >= KB_CODE_SVKL) {
        pressShifted(k, (short)(code - KB_CODE_SVKL), modifiers);
    }
    else if (code >= KB_CODE_SVK) {
        pressShifted(k, (short)(code - KB_CODE_SVK), modifiers);
    }
    else if (code >= KB_CODE_VK) {
        press(k, (short)(code - KB_CODE_VK), modifiers);
    }
    else if (code == 8 || code == 9 || code == 13 || code == 32 || (code >= '0' && code <= '9')) {
        press(k, (short)code, modifiers);
    }
    else if (code >= 'a' && code <= 'z') {
        press(k, (short)(code - 32), modifiers);
    }
    else if (code >= 'A' && code <= 'Z') {
        pressShifted(k, (short)code, modifiers);
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
