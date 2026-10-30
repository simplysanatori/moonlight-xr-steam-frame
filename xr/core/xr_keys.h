// The in world keyboard's own rules: which sheet a press leaves it on, which
// of Ctrl, Alt and Win are held, and what goes to the host with them. Plain
// arithmetic over the codes Java's layout hands down, no context, so the host
// tests reach all of it.

#ifndef XR_KEYS_H
#define XR_KEYS_H

#include "xr_shared.h"

// What one press does
typedef struct {
    // The sheet showing after it, a KB_STATE_
    int sheet;
    // The modifiers lit after it, KB_MOD_ bits
    int mods;
    // What to type, or -1 for nothing, and the modifiers held while it goes
    int code;
    int codeMods;
    // 1 if it put the keyboard away
    int hide;
} KbPress;

// A press on a key with this code, on this sheet, with these modifiers lit.
// A modifier key lights or puts out its own bit. Any other key that types
// goes with whatever is lit and puts them all out. The sheet keys leave the
// modifiers alone, so a Ctrl lit on the Fn sheet still holds over the
// letters, and hiding the keyboard lets go of them.
KbPress kbPress(int sheet, int mods, int code);

// The modifier a key code lights, a KB_MOD_ bit, or 0
int kbModifierBit(int code);

// ---- the media keys row (PLAN 3.11) -------------------------------------------------------
// Six keys in a row under the main block, on both keyboards, every sheet and
// every key map: they send the host's own media keys, so what they do is the
// PC's (its volume, its player), not the stream's. Left to right: previous
// track, play/pause, next track, mute, volume down, volume up.
#define KB_MEDIA_KEYS 6
// The Windows virtual key of media key i (0 .. KB_MEDIA_KEYS - 1), or 0
int kbMediaVk(int i);
// The key code the keyboards' tables carry for it: KB_CODE_VK + the VK
int kbMediaCode(int i);
// Which media key a table code is, or -1
int kbMediaIndex(int code);
// The key's rectangle on the keyboard panel (0..1: x0, y0, x1, y1)
void kbMediaRect(int i, float r[4]);
// The main block's rows were laid out against KB_MAIN_TEX_H rows; this turns
// a v measured against them into the panel's, which is KB_TEX_H tall
float kbMainV(float v);

// ---- sticky modifiers on the Compact keyboard (PLAN 4b.13) ----------------------------------
// The Full keyboard's rule, now on the Compact one: a tap lights a modifier
// for the next key (kbPress, a second tap puts it out), and holding it for
// the hold time locks it until it is tapped again. Ctrl, Alt and Win lock
// their KB_MOD_ bit; Shift, which on this keyboard is a sheet, locks the
// capitals sheet (KB_STICKY_SHIFT, which is KBF_LSHIFT's value, so a locked
// mask can go to the panel as the Full keyboard's does).
#define KB_STICKY_SHIFT 0x01

typedef struct {
    // KB_MOD_ bits and KB_STICKY_SHIFT locked
    int locked;
    // The bit of the key under the trigger, or 0
    int holdBit;
    long long holdStartNs;
} KbSticky;

void kbStickyInit(KbSticky* s);
// The bit a key code locks, or 0
int kbStickyBit(int code);
// A press on a key with this code. A tap on a locked modifier lets it go and
// returns 1 (the press that follows, kbPress, puts out the light); a press on
// one that is not locked starts its hold clock. Other keys do nothing.
int kbStickyPress(KbSticky* s, int code, long long nowNs);
// Once a frame while the keyboard is up; down is whether the trigger is still
// on the key pressed. Returns the bit that just locked, or 0.
int kbStickyTick(KbSticky* s, int down, long long nowNs, long long holdNs);
// The lit modifiers once a press has been applied: those locked stay lit
int kbStickyMods(const KbSticky* s, int mods);
// The sheet once a press has been applied: a locked Shift keeps the capitals
int kbStickySheet(const KbSticky* s, int sheet);

#endif
