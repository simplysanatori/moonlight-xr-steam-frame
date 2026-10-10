// Moonlight XR Native: the in-world keyboard's presses as host key events,
// ported from Moonlight XR's VrKeyboard.java (Gilleece, GPLv3). Letters,
// digits, the control keys and the keys that type nothing go as virtual key
// codes, capitals inside a held shift, other characters as text. Ctrl, Alt and
// Win are held down on the host for as long as they are lit on the keyboard.
#pragma once

typedef struct VrKeyboardSink {
    void* user;
    // keyCode: Windows VK; action: KEY_ACTION_DOWN / UP; modifiers: MODIFIER_*
    void (*key)(void* user, short keyCode, char action, char modifiers);
    void (*text)(void* user, const char* utf8);
} VrKeyboardSink;

typedef struct VrKeyboard {
    VrKeyboardSink sink;
    int held;   // KB_MOD_* the host has been told are down
} VrKeyboard;

void vrkb_init(VrKeyboard* k, VrKeyboardSink sink);
// A key typed on the keyboard (Unicode, or KB_CODE_VK + vk) with the
// modifiers lit as it was pressed
void vrkb_type(VrKeyboard* k, int code, int mods);
// Ctrl, Alt and Win as lit on the keyboard now (KB_MOD_* bits)
void vrkb_hold(VrKeyboard* k, int mods);
void vrkb_release_all(VrKeyboard* k);
// The US layout's virtual key for punctuation with a key of its own, or 0
short vrkb_punctuation_key(int code);
