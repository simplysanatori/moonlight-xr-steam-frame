// Moonlight XR Native: the headset keyboard's two layouts on the input side.
// Compact is Moonlight XR's laser keyboard as ported (xr_input.c's pressKey);
// Full is the tenkeyless one (xr_kbfull.h). This is what flips between them,
// fills the hit tables the ray tests against (kbKeyRects, kbCodes), turns a
// press on a Full key into key events in the IN_KEY slot, and lets go of
// everything held when the keyboard goes away. GPLv3.
#ifndef XR_KEYBOARD_H
#define XR_KEYBOARD_H

#include "xr_renderer.h"

// The Full keyboard hangs a little larger than Compact: more, smaller keys
#define KB_FULL_SCALE 1.2f

// The layout showing: the choice, but Compact while the report sheet is up
// (its fields take characters, not key codes)
int xrKeyboardLayout(XrCtx* ctx);
// Fills kbKeyCount, kbKeyRects and kbCodes for the layout showing
void xrKeyboardFillTables(XrCtx* ctx);
// The start of an input pass, and again at its end: when the layout showing
// changed, lets go of what the other one held, refills the tables and puts
// the panel at its size
void xrKeyboardSync(XrCtx* ctx, float* out);
// A press on key (an index into the tables) by the trigger of hand. Returns 1
// if it was taken here (a Full key, or the layout key), 0 for a Compact key
// that pressKey handles.
int xrKeyboardPress(XrCtx* ctx, float* out, int key, int hand, int64_t now);
// Once an input pass, last: a held Full key repeats or comes up with the
// trigger, a held modifier locks, and everything is let go when the keyboard
// is away or nothing is read (live == 0). Sets kbMods to the modifiers down.
void xrKeyboardFrame(XrCtx* ctx, float* out, int64_t now, int live);
// Sets the choice (a tap on the layout key, or config.ini) and asks for it to
// be stored
void xrKeyboardSetLayout(XrCtx* ctx, float* out, int layout);

#endif
