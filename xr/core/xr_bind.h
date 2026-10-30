// Controller bindings: which Frame control does what, in pointer mode and in
// gamepad mode, for every app or for one host app by name. The table comes
// from built in defaults (today's fixed maps, plus the seven controls pointer
// mode used to leave idle) with ~/.config/moonlight-xr/bindings.ini layered
// over them. Plain parsing and table lookups, no OpenXR calls and no context,
// so the host tests reach all of it. docs/dev/bindings.md has the file
// format and the whole default file. GPLv3.
//
// What a binding can never change (the parser refuses these with an error,
// and the table helpers below keep them working whatever is bound):
// - The pointer/gamepad switch. A chord that is one of the three switch
//   shortcuts (left.view+left.grip, left.stick_click+right.stick_click,
//   left.trigger+right.trigger+left.grip+right.grip) is refused in every
//   mode, whichever shortcut gamepad_toggle picks, so changing that setting
//   never turns a binding into a clash. The controls themselves stay
//   bindable one by one: the shortcut's own hold logic (xr_gamepad.h) runs
//   before the bindings and keeps its parts off them.
// - The disconnect chord of gamepad mode, left.view+right.menu+left.bumper+
//   right.bumper (Start, Back, LB and RB as they are by default). A chord
//   holding all four is refused. In gamepad mode the four held together go
//   out as Start, Back, LB and RB whatever they are bound to, and a chord
//   made of some of them never fires while another of them, or anything
//   else bound to LB or RB (the grips by default), is down: the way the
//   Guide chord gives way to it today.
// - Grip grab over our UI. In pointer mode the grips grab the move pill, a
//   screen corner, the keyboard's bar and the stats window whatever they are
//   bound to; the caller leaves them out of the controls it hands in while
//   the ray is on our UI. A chord with a grip in it is refused in pointer
//   mode, since half of it would be a grab.
// - The system button (SteamVR's) is no control here at all.

#ifndef XR_BIND_H
#define XR_BIND_H

#include <stddef.h>
#include <stdint.h>

#include "xr_gamepad.h"

// ---- the controls (sources) ---------------------------------------------------------------
// Every Steam Frame control an app gets, both hands. A whole stick is its two
// axes together (stick: actions); its four directions and its click are
// buttons like the rest. The touch controls are the capacitive sensors the
// probe lists (docs/probe/openxr-probe.txt), not bound to OpenXR actions yet.
enum {
    XR_BIND_SRC_LEFT_TRIGGER,
    XR_BIND_SRC_LEFT_GRIP,
    XR_BIND_SRC_LEFT_BUMPER,
    XR_BIND_SRC_LEFT_STICK,
    XR_BIND_SRC_LEFT_STICK_UP,
    XR_BIND_SRC_LEFT_STICK_DOWN,
    XR_BIND_SRC_LEFT_STICK_LEFT,
    XR_BIND_SRC_LEFT_STICK_RIGHT,
    XR_BIND_SRC_LEFT_STICK_CLICK,
    XR_BIND_SRC_LEFT_VIEW,
    XR_BIND_SRC_LEFT_DPAD_UP,
    XR_BIND_SRC_LEFT_DPAD_DOWN,
    XR_BIND_SRC_LEFT_DPAD_LEFT,
    XR_BIND_SRC_LEFT_DPAD_RIGHT,
    XR_BIND_SRC_LEFT_TRIGGER_TOUCH,
    XR_BIND_SRC_LEFT_STICK_TOUCH,
    XR_BIND_SRC_RIGHT_TRIGGER,
    XR_BIND_SRC_RIGHT_GRIP,
    XR_BIND_SRC_RIGHT_BUMPER,
    XR_BIND_SRC_RIGHT_STICK,
    XR_BIND_SRC_RIGHT_STICK_UP,
    XR_BIND_SRC_RIGHT_STICK_DOWN,
    XR_BIND_SRC_RIGHT_STICK_LEFT,
    XR_BIND_SRC_RIGHT_STICK_RIGHT,
    XR_BIND_SRC_RIGHT_STICK_CLICK,
    XR_BIND_SRC_RIGHT_A,
    XR_BIND_SRC_RIGHT_B,
    XR_BIND_SRC_RIGHT_X,
    XR_BIND_SRC_RIGHT_Y,
    XR_BIND_SRC_RIGHT_MENU,
    XR_BIND_SRC_RIGHT_TRIGGER_TOUCH,
    XR_BIND_SRC_RIGHT_STICK_TOUCH,
    XR_BIND_SRC_RIGHT_A_TOUCH,
    XR_BIND_SRC_RIGHT_B_TOUCH,
    XR_BIND_SRC_RIGHT_X_TOUCH,
    XR_BIND_SRC_RIGHT_Y_TOUCH,
    XR_BIND_SRC_COUNT
};

// A set of controls as bits, one per source: a single control or a chord
#define XR_BIND_BIT(src) ((uint64_t)1 << (src))
// A chord is two to four controls held together
#define XR_BIND_CHORD_MAX_PARTS 4

// The file's name for a control ("right.y"), or NULL out of range
const char* xrBindSourceName(int src);
// A control by its name, case blind, or -1
int xrBindSourceFind(const char* name);
// 1 for the two whole sticks, which only take stick: actions
int xrBindSourceIsStick(int src);

// ---- the actions ------------------------------------------------------------------------
#define XR_BIND_ACT_NONE 0
// key:[ctrl+][shift+][alt+][win+]<name or VK>: a Windows virtual key code
#define XR_BIND_ACT_KEY 1
// mouse:left|right|middle|x1|x2, a host mouse button
#define XR_BIND_ACT_MOUSE 2
// wheel:up|down|left|right, one notch per press
#define XR_BIND_ACT_WHEEL 3
// pad:<button>, gamepad mode's pad
#define XR_BIND_ACT_PAD 4
// stick:<what>, a whole stick only
#define XR_BIND_ACT_STICK 5
// app:<command>, something of ours
#define XR_BIND_ACT_APP 6

// The modifiers a key goes with: Moonlight's MODIFIER_ bits, which the
// keyboard's KB_MOD_ bits (xr_shared.h) and sendKey's byte are as well
#define XR_BIND_MOD_SHIFT 0x01
#define XR_BIND_MOD_CTRL 0x02
#define XR_BIND_MOD_ALT 0x04
#define XR_BIND_MOD_WIN 0x08

// mouse: codes. Button n is the VR_BUTTON_ bit 1 << n (xr_renderer.h: LEFT 1,
// RIGHT 2, MIDDLE 4), widened to five with X1 (8) and X2 (16).
#define XR_BIND_MOUSE_LEFT 0
#define XR_BIND_MOUSE_RIGHT 1
#define XR_BIND_MOUSE_MIDDLE 2
#define XR_BIND_MOUSE_X1 3
#define XR_BIND_MOUSE_X2 4
#define XR_BIND_MOUSE_BUTTONS 5

// wheel: codes
#define XR_BIND_WHEEL_UP 0
#define XR_BIND_WHEEL_DOWN 1
#define XR_BIND_WHEEL_LEFT 2
#define XR_BIND_WHEEL_RIGHT 3

// pad: codes are the PAD_ button bits (xr_shared.h), PAD_GUIDE included, and
// for the two triggers the PAD_HELD_LT and PAD_HELD_RT bits (xr_gamepad.h):
// a trigger bound to them goes on as it reads, a button pulls them all the way

// stick: codes
#define XR_BIND_STICK_SCROLL 0
#define XR_BIND_STICK_MOUSE 1
#define XR_BIND_STICK_WASD 2
#define XR_BIND_STICK_ARROWS 3
#define XR_BIND_STICK_DPAD 4
#define XR_BIND_STICK_PAD_LEFT 5
#define XR_BIND_STICK_PAD_RIGHT 6

// app: codes
// Show or hide the in world keyboard
#define XR_BIND_APP_KEYBOARD 0
// Step the ray setting: off, dot, ray
#define XR_BIND_APP_RAY 1
// What SteamVR's recentre does: the screen comes round to where you face
#define XR_BIND_APP_RECENTRE 2
// Open or close the cog panel
#define XR_BIND_APP_COG 3
// 3D on or off
#define XR_BIND_APP_3D 4
// Switch between pointer and gamepad mode at once (no hold)
#define XR_BIND_APP_GAMEPAD 5
// Bring the screen to in front of the controller (PLAN 3.6)
#define XR_BIND_APP_BRINGHERE 6
// Open or close the Host sheet (keys for the PC, PLAN 4b.1)
#define XR_BIND_APP_HOST 7
// Zoom the picture one rung in or out, aimed at the pointer, and back to the
// whole picture (PLAN 4b.6)
#define XR_BIND_APP_ZOOMIN 8
#define XR_BIND_APP_ZOOMOUT 9
#define XR_BIND_APP_ZOOMRESET 10
#define XR_BIND_APP_COUNT 11

typedef struct {
    // XR_BIND_ACT_
    int kind;
    // The kind's code: a VK, a mouse or wheel or stick or app code, PAD_ bits
    int code;
    // XR_BIND_MOD_ bits, for a key
    int mods;
} XrBindAction;

// One action from its text ("key:ctrl+F", "mouse:x1", "none"). Returns 1, or
// 0 with why in err (cap bytes) and where in the text it went wrong in *col
// (0 based); col may be NULL.
int xrBindActionParse(const char* text, XrBindAction* out, char* err, size_t cap, int* col);
// An action as the file has it, the way xrBindActionParse reads it back.
// Returns the length, snprintf's way.
int xrBindActionFormat(const XrBindAction* a, char* out, size_t cap);
int xrBindActionEqual(const XrBindAction* a, const XrBindAction* b);

// A key's name, case blind ("f", "esc", "pagedown", "f12"), as a VK, or 0
int xrBindKeyFind(const char* name);
// The file's name for a VK, or NULL when it has none (it is written 0xNN)
const char* xrBindKeyName(int vk);

// The host's mouse button (Moonlight's BUTTON_: 1 left, 2 middle, 3 right, 4
// X1, 5 X2) for a mouse: code, or 0. The first three are xr_session.c's MAP.
int xrBindHostButton(int mouseCode);

// ---- the modes and the file ----------------------------------------------------------------
#define XR_BIND_MODE_POINTER 0
#define XR_BIND_MODE_GAMEPAD 1
#define XR_BIND_MODES 2

// Where a resolved binding came from
#define XR_BIND_FROM_DEFAULT 0
#define XR_BIND_FROM_MODE 1
#define XR_BIND_FROM_APP 2

#define XR_BIND_MAX_RULES 256
#define XR_BIND_MAX_APPS 32
#define XR_BIND_APP_MAX 96
// Chords per section and mode, and in a resolved table (the defaults' one,
// the mode's and the app's)
#define XR_BIND_MAX_SECTION_CHORDS 8
#define XR_BIND_MAX_CHORDS 24
#define XR_BIND_LINE_MAX 512

// One line of the file
typedef struct {
    // -1 for [pointer] or [gamepad], else the app's index
    int app;
    int mode;
    // One control, or a chord's
    uint64_t sources;
    XrBindAction action;
    int line;
} XrBindRule;

// The file as read: its rules in order and the apps they name
typedef struct {
    int ruleCount;
    XrBindRule rules[XR_BIND_MAX_RULES];
    int appCount;
    char apps[XR_BIND_MAX_APPS][XR_BIND_APP_MAX];
} XrBindSet;

#define XR_BIND_MAX_ERRORS 16
#define XR_BIND_ERROR_MAX 192

typedef struct {
    // 1 based, 0 for the file as a whole
    int line;
    int column;
    // "line:column: what", whole
    char text[XR_BIND_ERROR_MAX];
} XrBindError;

typedef struct {
    // Every error, of which the first XR_BIND_MAX_ERRORS are kept
    int count;
    XrBindError errors[XR_BIND_MAX_ERRORS];
} XrBindErrors;

// Nothing bound: the defaults alone
void xrBindSetInit(XrBindSet* s);

// Reads a bindings.ini text into s (which it starts over). Strict: a line
// that is not right (an unknown section, control or action, a control bound
// twice in one section, a reserved chord, a line too long) is left out with
// an error, and every other line still counts. errs may be NULL. Returns the
// number of errors.
int xrBindParse(XrBindSet* s, const char* text, XrBindErrors* errs);

// s as a file xrBindParse reads back to the same rules (the editor's save).
// Returns the length, snprintf's way.
int xrBindSerialize(const XrBindSet* s, char* out, size_t cap);

// The file written on the first run: what the format is, and every default
// as a commented out line, so it changes nothing until a line is uncommented.
// Returns the length, snprintf's way.
int xrBindDefaultText(char* out, size_t cap);

// $MLXR_CONFIG_DIR/bindings.ini, else ~/.config/moonlight-xr/bindings.ini
const char* xrBindDefaultPath(char* buf, size_t cap);

// Reads the file at path (NULL for the default path) into s. With no file,
// writes xrBindDefaultText there (and its folder) and returns 1; 0 when read;
// -1 when it could not be read or written (s is then the defaults alone, and
// errs says why).
int xrBindLoadFile(XrBindSet* s, const char* path, XrBindErrors* errs);

// ---- the resolved table -------------------------------------------------------------------
typedef struct {
    uint64_t parts;
    XrBindAction action;
    int from;
} XrBindChord;

typedef struct {
    int mode;
    // Each control's own action, and where it came from (XR_BIND_FROM_)
    XrBindAction single[XR_BIND_SRC_COUNT];
    int from[XR_BIND_SRC_COUNT];
    // The chords bound to something
    int chordCount;
    XrBindChord chords[XR_BIND_MAX_CHORDS];
} XrBindTable;

// What every control does in one mode for one host app: the defaults, then
// the mode's section, then the app's own lines for that mode ([app "Name"],
// the name matched case blind against Settings.app). s NULL is the defaults
// alone, app NULL or empty no app.
void xrBindResolve(const XrBindSet* s, int mode, const char* app, XrBindTable* out);

// ---- the table at work ---------------------------------------------------------------------------
// The controls down this frame off both controllers as the pad reads them
// (after padChordApply), as bits. was is last frame's, for the hysteresis:
// a trigger or grip is down past PAD_GRIP_ON and up under PAD_GRIP_OFF, a
// stick direction down past XR_BIND_STICK_ON and up under XR_BIND_STICK_OFF.
// The touch controls are not in PadHand; the caller adds their bits.
#define XR_BIND_STICK_ON 0.5f
#define XR_BIND_STICK_OFF 0.3f
uint64_t xrBindSourcesDown(const PadHand* left, const PadHand* right, uint64_t was);

// The actions held this frame for the controls down: each control's own,
// and a chord's once all of its parts are down, which then takes those parts'
// own actions out. Nothing is timed here (no window, no hold): the caller
// runs chords through its own timing where that matters (Guide's
// padGuideStep). In gamepad mode the disconnect chord's four controls held
// together are Start, Back, LB and RB whatever they are bound to. Whole
// sticks are never down; their stick: action is read off the table. Returns
// how many it wrote, at most cap.
int xrBindHeld(const XrBindTable* t, uint64_t down, XrBindAction* out, int cap);

// The host mouse buttons held, as VR_BUTTON_ bits (1 << the mouse: code)
int xrBindMouseMask(const XrBindTable* t, uint64_t down);

// The pad buttons held, as PAD_ bits (the triggers' PAD_HELD_ codes are not
// buttons and are left out)
int xrBindPadButtons(const XrBindTable* t, uint64_t down);

#endif
