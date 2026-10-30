// The Full keyboard: a tenkeyless layout (87 keys) and the rules for its
// modifiers, auto-repeat and Caps Lock, like Hacker's Keyboard on a phone.
// Plain arithmetic over times handed in, no context, so the host tests reach
// all of it. GPLv3.
//
// Modifiers (Shift, Ctrl, Alt, Win, left and right): a tap lights one for the
// next key that is not a modifier (one-shot: it goes down on the host at the
// tap and up after that key); holding it for kbfHoldNs locks it down until it
// is tapped again; tapping a lit one-shot cancels it. Any other key goes down
// when pressed, repeats while held (after kbfRepeatDelayNs, then 30 a second)
// and goes up when let go, like a physical key.

#ifndef XR_KBFULL_H
#define XR_KBFULL_H

#include <stdint.h>

#include "xr_shared.h"

// The two layouts, as keyboard_layout in config.ini
#define KB_LAYOUT_COMPACT 0
#define KB_LAYOUT_FULL 1

// The key that flips between the layouts, in the key code tables
#define KB_CODE_LAYOUT -9
// The id the input pass hands back (IN_SETTING) to store keyboard_layout
#define SETTING_KEYBOARD_LAYOUT 61

// Modifier bits. The low three of the left side are the KB_MOD_ values the
// Compact keyboard already used; Shift (bit 0) is new, and so are the right
// hand keys.
#define KBF_LSHIFT 0x01
#define KBF_LCTRL 0x02  /* == KB_MOD_CTRL */
#define KBF_LALT 0x04   /* == KB_MOD_ALT */
#define KBF_LWIN 0x08   /* == KB_MOD_WIN */
#define KBF_RSHIFT 0x10
#define KBF_RCTRL 0x20
#define KBF_RALT 0x40
#define KBF_RWIN 0x80
#define KBF_SHIFTS (KBF_LSHIFT | KBF_RSHIFT)
#define KBF_MOD_MASK 0xFF

// Flags the Full keyboard adds to the modifiers it hands the key slot
// (IN_KEY_MODS): the key goes down only, or up only. Without either the key
// is typed whole, as the Compact keyboard does.
#define KBF_FLAG_DOWN 0x10000
#define KBF_FLAG_UP 0x20000
// Native (the Host sheet and macros, xr_hostkeys.h): the key is tapped whole
// with these modifiers held round it, and the modifiers it brought go up
// after it. HOLD puts the KBF_ modifier bits in the mods down on the host and
// LET takes them up; both carry no key of their own.
#define KBF_FLAG_CHORD 0x40000
#define KBF_FLAG_HOLD 0x80000
#define KBF_FLAG_LET 0x100000

#define KBF_KEY_COUNT 88  /* 87 keys and the layout key */

// The key maps (config.ini keyboard_keymap): what the keys say, and so which
// Windows layout the PC has to be on. Every key keeps the virtual key of the
// US keyboard's key in that place; the host's layout turns it into the
// character (Sunshine and Apollo send each as the US scan code), so only the
// labels change. The French and German boards also have a key beside the left
// Shift (VK_OEM_102, "<" and ">"): the Full keyboard grows it, at index
// KBF_ISO_KEY, and the left Shift is the shorter for it.
#define KB_MAP_QWERTY 0
#define KB_MAP_AZERTY 1
#define KB_MAP_QWERTZ 2
#define KB_MAP_COUNT 3
// Native (PLAN 3.11): the media keys row follows the 88, on every map: previous
// track, play/pause, next track, mute, volume down, volume up (xr_keys.h
// kbMedia*). Under them the ISO key, where a map has one.
#define KBF_MEDIA_FIRST KBF_KEY_COUNT
#define KBF_MEDIA_COUNT 6
#define KBF_KEY_COUNT_ALL (KBF_KEY_COUNT + KBF_MEDIA_COUNT)
#define KBF_ISO_KEY KBF_KEY_COUNT_ALL
#define KBF_KEY_COUNT_MAX (KBF_KEY_COUNT_ALL + 1)

// What a key does
enum { KBF_NORMAL = 0, KBF_MOD, KBF_CAPS, KBF_LAYOUT };

typedef struct {
    const char* label;    // what it says unshifted; "@up" etc are drawn arrows
    const char* shifted;  // with Shift (or Caps, for a letter); NULL = same
    int vk;               // Windows virtual key; 0 for the layout key
    int ext;              // the scan code is extended (E0 prefix)
    int kind;             // KBF_NORMAL / KBF_MOD / KBF_CAPS / KBF_LAYOUT
    int modBit;           // KBF_ bit of a modifier key
    int letter;           // 1 for A..Z, which Caps shifts as well
    float x, y, w, h;     // in key units: 18.25 wide, 6.5 tall
} KbfKey;

#define KBF_UNITS_W 18.25f
#define KBF_UNITS_H 6.5f

const KbfKey* kbfKey(int index);
// The keys the Full keyboard has on a map: KBF_KEY_COUNT_ALL (the 88 and the
// media keys), and the ISO key too on AZERTY and QWERTZ. Out of range gives
// QWERTY.
int kbfKeyCountFor(int map);
// The key's rectangle on the panel (0..1: x0, y0, x1, y1)
void kbfKeyRect(int index, float r[4]);
void kbfKeyRectFor(int map, int index, float r[4]);
// The key code the panel's tables carry: KB_CODE_VK + vk, or KB_CODE_LAYOUT
int kbfKeyCode(int index);
// The key whose code is vk (a modifier by its own side), or -1
int kbfFindVk(int vk);
// The label to draw given the modifiers down (KBF_ mask) and Caps Lock
const char* kbfLabel(int index, int mods, int caps);
const char* kbfLabelFor(int map, int index, int mods, int caps);
// The Moonlight modifier flags (MODIFIER_*) for a KBF_ mask
int kbfModifierByte(int mods);

typedef enum { KBF_EV_NONE = 0, KBF_EV_DOWN, KBF_EV_UP, KBF_EV_LAYOUT } KbfEventKind;

typedef struct {
    int kind;
    int vk;
    int ext;
    // KBF_ modifiers down at the time
    int mods;
} KbfEvent;

typedef struct KbFull {
    int oneshot;       // KBF_ bits lit for the next key
    int locked;        // KBF_ bits locked down
    int caps;          // Caps Lock as we believe the host has it
    // The key under the trigger, or -1, and who is holding it
    int held;
    int hand;
    int64_t pressNs;
    int64_t nextRepeatNs;
    int longDone;      // the hold already locked it
    int wasState;      // a modifier's state when it was pressed: 0 off, 1 one-shot, 2 locked
    int64_t holdNs;
} KbFull;

#define KBF_HOLD_NS_DEFAULT 500000000LL
#define KBF_REPEAT_DELAY_NS 450000000LL
#define KBF_REPEAT_INTERVAL_NS 33333333LL

void kbfInit(KbFull* k);
void kbfSetHoldMs(KbFull* k, int ms);
// The modifiers down on the host: one-shot and locked
int kbfDownMods(const KbFull* k);
// A press on a key. Fills ev for a key going down or the layout key.
void kbfPress(KbFull* k, int key, int hand, int64_t now, KbfEvent* ev);
// The trigger let go of the held key
void kbfRelease(KbFull* k, int64_t now, KbfEvent* ev);
// Once a frame while a key is held: promotes a held modifier to locked, and
// repeats a held key
void kbfTick(KbFull* k, int64_t now, KbfEvent* ev);
// Lets go of everything: the held key comes up (ev), and the one-shot and
// locked modifiers are cleared (the caller sends the new down mask). Caps
// stays: it is the host's.
void kbfReleaseAll(KbFull* k, KbfEvent* ev);

#endif
