// Moonlight XR Native: the in-world keyboard's presses as host key events,
// ported from Moonlight XR's VrKeyboard.java (Gilleece, GPLv3). Letters,
// digits, the control keys and the keys that type nothing go as virtual key
// codes, capitals inside a held shift, other characters as text. Ctrl, Alt and
// Win are held down on the host for as long as they are lit on the keyboard.
#pragma once
#include <stddef.h>

typedef struct VrKeyboardSink {
    void* user;
    // keyCode: Windows VK; action: KEY_ACTION_DOWN / UP; modifiers: MODIFIER_*
    void (*key)(void* user, short keyCode, char action, char modifiers);
    void (*text)(void* user, const char* utf8);
} VrKeyboardSink;

typedef struct VrKeyboard {
    VrKeyboardSink sink;
    int held;   // KBF_* modifiers (xr_kbfull.h) the host has been told are down
    int heldKey; // a Full keyboard key sent down and not yet up (its VK), or 0
} VrKeyboard;

void vrkb_init(VrKeyboard* k, VrKeyboardSink sink);
// A key typed on the keyboard (Unicode, or KB_CODE_VK + vk) with the
// modifiers lit as it was pressed
void vrkb_type(VrKeyboard* k, int code, int mods);
// A code of KB_CODE_VK + vk with KBF_FLAG_DOWN or KBF_FLAG_UP in mods (the Full
// keyboard) is that one event, so a key can be held and repeat like a real one.
// mods then are the KBF_* modifiers down with it.
// KBF_FLAG_CHORD taps the key with mods held round it and lets the ones it
// brought go after it; KBF_FLAG_HOLD and KBF_FLAG_LET (no key of their own)
// put the mods down on the host and take them up (the Host sheet's macros).
// Shift, Ctrl, Alt and Win, either side, as lit on the keyboard now (KBF_* bits,
// which for the left Ctrl, Alt and Win are the old KB_MOD_* bits)
void vrkb_hold(VrKeyboard* k, int mods);
void vrkb_release_all(VrKeyboard* k);
// Native (lane wave4-apollo): types a whole text as the host's keyboard would take it, the text path a
// character from the keyboard's symbol sheets goes by (sink.text, in pieces of at most
// VRKB_TEXT_CHUNK bytes cut between characters), except that a line end is the Enter key and a tab
// the Tab key, since text events do not carry them. Other control characters and bytes that are not
// UTF-8 are left out. Stops before the character that would pass maxBytes of the text; returns the
// bytes of utf8 used (all of it when it fits), which the caller compares with strlen to say it was cut.
#define VRKB_TEXT_CHUNK 64
size_t vrkb_type_text(VrKeyboard* k, const char* utf8, size_t maxBytes);
// The US layout's virtual key for punctuation with a key of its own, or 0
short vrkb_punctuation_key(int code);
