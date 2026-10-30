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

// ---- the PC page (lane wave4-apollo, PLAN 4.6, 4b.4, 4b.5) -----------------------------------------
// A second page of the sheet, reached by a button in its header: "Paste from PC", "Send text to PC"
// and the host's own commands (Apollo's server commands). Its cells sit on the same grid. This block is
// kept apart from the main page's entries above.
#define XR_HOST_PAGE_MAIN 0
#define XR_HOST_PAGE_PC 1
// The header button that flips between the pages (a point on it is this zone)
#define XR_HOST_ZONE_PAGE 103

// PC page cells: the two clipboard entries on the first row, the commands from the second
#define XR_HOST_PC_PASTE 0
#define XR_HOST_PC_COPY 1
#define XR_HOST_PC_CMD_FIRST XR_HOST_COLS
#define XR_HOST_CMD_SLOTS 8
#define XR_HOST_CMD_NAME 64
// What a PC page cell is
#define XR_HOST_CELL_PASTE 3
#define XR_HOST_CELL_COPY 4
#define XR_HOST_CELL_CMD 5

void xrHostPageRect(float r[4]);
// What a PC page cell is, given how many commands the host listed: XR_HOST_CELL_PASTE, _COPY, _CMD
// (cell - XR_HOST_PC_CMD_FIRST is its number) or XR_HOST_CELL_NONE
int xrHostPcCellKind(int cell, int commands);
// xrHostZoneAt for either page; page XR_HOST_PAGE_MAIN is xrHostZoneAt's answer. The page button counts
// on both, and a confirmation hides it like it hides Close.
int xrHostZoneAtPage(float u, float v, int page, int macros, int commands, int confirm);

// What the host offers, which the app learns from serverinfo when a stream starts and the PC page
// shows. cmdWhy is why there are no commands to run (XR_HOSTINFO_*), 0 when there are.
#define XR_HOSTINFO_OK 0
#define XR_HOSTINFO_NOT_APOLLO 1
#define XR_HOSTINFO_NO_PERMISSION 2
#define XR_HOSTINFO_NONE_SET 3

typedef struct {
    int apollo;       // the host speaks Apollo
    int clipRead;     // this client may read its clipboard
    int clipSet;      // and set it
    int cmdWhy;       // XR_HOSTINFO_*
    int cmdCount;
    char cmd[XR_HOST_CMD_SLOTS][XR_HOST_CMD_NAME];
} XrHostInfo;

// What a press on the PC page asks the app to do (XrAppHooks.hostAction)
#define XR_HOSTACT_NONE 0
#define XR_HOSTACT_PASTE 1   // fetch the host's clipboard and type it
#define XR_HOSTACT_COPY 2    // set the host's clipboard to a text
#define XR_HOSTACT_CMD 3     // run server command number arg

// "Send text to PC": the keyboard takes text instead of sending keys, into a field of its own, and
// Enter sends it. Plain editing, so the tests reach all of it.
#define XR_COMPOSE_MAX 1024
typedef struct {
    int active;
    int len;
    char text[XR_COMPOSE_MAX];
} XrHostCompose;

enum {
    XR_COMPOSE_IGNORE = 0,   // not the field's: the keyboard does its own thing with it
    XR_COMPOSE_EDITED,       // a character in, one out (Backspace), or nothing room for
    XR_COMPOSE_SEND          // Enter: the text is complete
};

void xrHostComposeStart(XrHostCompose* c);
void xrHostComposeStop(XrHostCompose* c);
// A key by its table code: a character (Unicode below KB_CODE_VK), 8 Backspace, 13 Enter
int xrHostComposeKey(XrHostCompose* c, int code);
// The last `chars` characters of the text, with a leading ellipsis when it was longer (for the space bar)
void xrHostComposeTail(const char* text, int chars, char* out, size_t cap);

#endif
