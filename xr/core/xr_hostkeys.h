// The Host sheet's keys (PLAN 4b.1, 4b.2): the quick actions it offers, the
// user's macros from macros.ini, the runner that plays a key sequence out one
// event a frame, and the "Send keys" chord capture. Plain parsing and
// arithmetic, no context and no OpenXR, so the host tests reach all of it.
// docs/dev/host.md has the file format. GPLv3.
//
// Every key goes out through the keyboard's own path (IN_KEY / IN_KEY_MODS to
// vrkb_type), so a quick action, a macro step and a typed key are the same
// thing to the host. KBF_ modifier bits (xr_kbfull.h) are the Moonlight
// MODIFIER_ bits and xr_bind's XR_BIND_MOD_ bits, so key specs parse with
// xr_bind's.

#ifndef XR_HOSTKEYS_H
#define XR_HOSTKEYS_H

#include <stddef.h>

// ---- the sheet's grid -----------------------------------------------------------------------
// 20 cells in 5 columns by 4 rows: the 11 quick actions, "Send keys", then up
// to 8 of the user's macros. Native pixels of the panel (UI_PANEL_HOST), and
// the same numbers give the hit zones, so the drawing and the ray agree.
#define XR_HOST_TEX_W 1024
#define XR_HOST_TEX_H 448
#define XR_HOST_COLS 5
#define XR_HOST_ROWS 4
#define XR_HOST_QUICK_COUNT 11
#define XR_HOST_CELL_SEND_KEYS XR_HOST_QUICK_COUNT
#define XR_HOST_MACRO_FIRST (XR_HOST_QUICK_COUNT + 1)
#define XR_HOST_MACRO_SLOTS 8
#define XR_HOST_CELLS (XR_HOST_MACRO_FIRST + XR_HOST_MACRO_SLOTS)

// Zones a point on the sheet can be in besides a cell (0 .. XR_HOST_CELLS - 1)
#define XR_HOST_ZONE_NONE (-1)
#define XR_HOST_ZONE_CLOSE 100
#define XR_HOST_ZONE_CONFIRM_SEND 101
#define XR_HOST_ZONE_CONFIRM_CANCEL 102

// What a cell is
#define XR_HOST_CELL_NONE (-1)
#define XR_HOST_CELL_QUICK 0
#define XR_HOST_CELL_KEYS 1
#define XR_HOST_CELL_MACRO 2

// Cell rectangle in panel UV (x0, y0, x1, y1), whether or not the cell is
// used; zeros out of range
void xrHostCellRect(int cell, float r[4]);
void xrHostCloseRect(float r[4]);
// The confirmation box, and its two buttons
void xrHostConfirmBox(float r[4]);
void xrHostConfirmSendRect(float r[4]);
void xrHostConfirmCancelRect(float r[4]);
// What a cell is, given how many macros there are: a quick action, Send keys,
// a macro, or XR_HOST_CELL_NONE for an empty slot
int xrHostCellKind(int cell, int macros);
// What a point on the sheet is on. While a confirmation is up (confirm) only
// its two buttons count. A cell with nothing in it is no zone.
int xrHostZoneAt(float u, float v, int macros, int confirm);

// ---- key sequences --------------------------------------------------------------------------
enum {
    // Tap the key with the modifiers round it (a chord): they go down, the key
    // goes down and up, they come up
    XR_HOST_STEP_TAP = 0,
    // Put a key down and leave it; a modifier key joins the ones held
    XR_HOST_STEP_DOWN,
    // Let a key go
    XR_HOST_STEP_UP,
    // Wait ms before the next step
    XR_HOST_STEP_WAIT
};

typedef struct {
    int kind;   // XR_HOST_STEP_
    int vk;     // Windows virtual key, for TAP, DOWN and UP
    int mods;   // KBF_ bits, for TAP
    int ms;     // for WAIT
} XrHostStep;

#define XR_HOST_STEPS 32
#define XR_HOST_WAIT_MAX_MS 5000

typedef struct {
    int count;
    XrHostStep step[XR_HOST_STEPS];
} XrHostSeq;

// ---- quick actions --------------------------------------------------------------------------
typedef struct {
    const char* label;   // the keys as the cell says them: "Ctrl+Alt+Tab"
    int mods;            // KBF_ bits
    int vk;
    int confirm;         // asks first: the effect is hard to take back
} XrHostQuick;

const XrHostQuick* xrHostQuick(int i);

// ---- macros ------------------------------------------------------------------------------------
#define XR_MACRO_MAX 16
#define XR_MACRO_NAME 28

typedef struct {
    char name[XR_MACRO_NAME];
    int confirm;
    XrHostSeq seq;
} XrMacro;

typedef struct {
    int count;
    XrMacro macro[XR_MACRO_MAX];
    // Lines left out, and the first of them as "line N: why"
    int errors;
    char error[200];
} XrMacros;

void xrMacrosClear(XrMacros* m);
// Parses the text of macros.ini over nothing (m is cleared first). A bad line
// is left out with an error and every other line still counts. Returns the
// number of macros kept.
int xrMacrosParse(XrMacros* m, const char* text);
// Reads a file. 1 if it was there (errors, if any, are in m), 0 if there is no
// such file (m is left empty) and -1 for another read error.
int xrMacrosLoadFile(XrMacros* m, const char* path);
// $MLXR_CONFIG_DIR/macros.ini, else ~/.config/moonlight-xr/macros.ini
const char* xrMacrosDefaultPath(char* buf, size_t cap);

// The sequence a cell plays, whether it asks first, and what to call it
// (label, cap bytes). Returns the cell's kind; XR_HOST_CELL_KEYS has no
// sequence of its own (it opens the keyboard in chord mode).
int xrHostCellSeq(int cell, const XrMacros* macros, XrHostSeq* seq, int* confirm, char* label,
                  size_t cap);

// The KBF_ bit of a modifier key's virtual key (Shift, Ctrl, Alt, Win, either
// side), or 0
int xrHostModBit(int vk);

// ---- playing a sequence --------------------------------------------------------------------------
enum {
    XR_HOST_EMIT_CHORD = 0,   // vk tapped with mods round it
    XR_HOST_EMIT_DOWN,        // vk down, mods held with it
    XR_HOST_EMIT_UP,          // vk up
    XR_HOST_EMIT_HOLD,        // the modifiers in mods go down and stay
    XR_HOST_EMIT_LET          // the modifiers in mods come up
};

typedef struct {
    int kind;   // XR_HOST_EMIT_
    int vk;
    int mods;   // KBF_ bits
} XrHostEmit;

typedef struct {
    XrHostSeq seq;
    int active;
    int pos;
    int waiting;
    long long waitUntilNs;
    // The modifiers this run holds down, and the other keys it put down and
    // has not let go
    int heldMods;
    int downKeys[4];
    int downCount;
    int aborting;
    // a run started over one that still held keys: they come up first
    int flush;
} XrHostRun;

void xrHostRunInit(XrHostRun* r);
// Starts playing seq. A run in progress is let go of first (its keys come up
// before the new one's first step); returns 0 if the sequence is empty.
int xrHostRunStart(XrHostRun* r, const XrHostSeq* seq);
int xrHostRunBusy(const XrHostRun* r);
// Skips what is left, but lets go of every key and modifier it holds
void xrHostRunAbort(XrHostRun* r);
// Once a frame, with the time. Returns 1 and the event to send this frame,
// or 0 for nothing to send (waiting, or finished). One event a frame, which is
// as fast as the key slot goes.
int xrHostRunStep(XrHostRun* r, long long nowNs, XrHostEmit* e);

// ---- "Send keys": type a combo, send it once ---------------------------------------------------------
typedef struct {
    int active;
    // KBF_ bits lit so far
    int mods;
} XrHostChord;

enum {
    XR_CHORD_IGNORE = 0,   // not for the chord: the keyboard does its own thing with it
    XR_CHORD_MOD,          // a modifier lit or put out
    XR_CHORD_SEND,         // the combo is complete: *vk and *mods are it, and the chord is over
    XR_CHORD_CANCEL        // Hide: the chord is over, nothing sent
};

void xrHostChordStart(XrHostChord* c);
void xrHostChordStop(XrHostChord* c);
// A press on a key of either keyboard, by its table code (a character, a
// KB_CODE_, or KB_CODE_VK + vk). A modifier lights or puts out its own bit;
// any key that has a virtual key completes the combo with the bits lit and
// whatever Shift the key carries (an upper case letter, KB_CODE_SVK); the
// sheet keys (?123, Fn, Shift, the layout key) are left to the keyboard.
int xrHostChordKey(XrHostChord* c, int code, int* vk, int* mods);

#endif
