// The two cards of the headset UI that teach the controls: the first-run
// card (shown once per install, config.ini's first_run_shown) and the pad
// card (a schematic of the physical gamepad in use, with live highlights,
// held in the lap while the pad is played). Plain arithmetic over values
// handed in, no clock of its own, so the host tests reach all of it.
//
// Native (lane wave3-cards). PLAN 3.9 and 2b.6. GPLv3.
#ifndef XR_CARDS_H
#define XR_CARDS_H

#include <stdint.h>

// ---- sizes ---------------------------------------------------------------------
// Native pixels of the panels' atlas slots, and the pad card's width in metres
#define FIRSTRUN_TEX_W 1024
#define FIRSTRUN_TEX_H 544   // lane fix-polish: 576 with the foot line it no longer has
#define PADCARD_TEX_W 768
#define PADCARD_TEX_H 432
#define PADCARD_WIDTH_M 0.30f

// ---- the first-run card ----------------------------------------------------------------
// Shown for this long, then gone by itself; a sheet opening puts it away at once
#define FIRSTRUN_SHOW_NS 20000000000LL
// Its width over the picture's furniture width, and how far in front of the picture it stands
#define FIRSTRUN_WIDTH_FRAC 0.55f
#define FIRSTRUN_LIFT_M 0.07f

typedef struct {
    int armed;           // not shown yet (the setting says so) and not put away
    int showing;         // on the screen now
    int64_t startNs;     // when it came up
    int markPending;     // it has come up: the caller writes first_run_shown, once
} FirstRun;

// shown: config.ini's first_run_shown. A shown card never comes back.
void firstRunInit(FirstRun* f, int shown);
// Once a frame. ready: a stream is up, the picture is showing and the session is
// focused. modal: a sheet (cog, keyboard, prompts, Host...) is open. Returns 1 while
// the card is to be drawn. It starts when ready first holds, goes after
// FIRSTRUN_SHOW_NS, or the moment a sheet opens (then it is also done for good).
int firstRunStep(FirstRun* f, int ready, int modal, int64_t nowNs);
// 1 once, the frame after the card first came up: write first_run_shown = true
int firstRunTakeMark(FirstRun* f);

// ---- the pad card ---------------------------------------------------------------------
// config.ini's pad_card, in the order its names list them
#define PADCARD_AUTO 0   // up while a pad is in use, gone PADCARD_AUTOHIDE_NS after the last input
#define PADCARD_ON 1     // up for as long as a pad counts as in use (gamepadsPhysicalActive)
#define PADCARD_OFF 2    // never
#define PADCARD_MODES 3
#define PADCARD_AUTOHIDE_NS 4000000000LL

// What the schematic looks like. The first four follow input/gamepads.h's GpGlyphSet
// (Xbox, PlayStation, Nintendo, Steam); Generic is a pad the app could not name.
#define PADSTYLE_XBOX 0
#define PADSTYLE_PLAYSTATION 1
#define PADSTYLE_NINTENDO 2
#define PADSTYLE_STEAM 3
#define PADSTYLE_GENERIC 4
#define PADSTYLE_COUNT 5
// glyphSet: GpGlyphSet's value; known: the pad announced a type of its own (not unknown)
int padStyleFor(int glyphSet, int known);

// A physical pad as the card draws it. The buttons are PAD_ bits (xr_shared.h), in
// positions: PAD_A is the bottom face button, B right, X left, Y top, whatever the
// pad prints on them.
typedef struct {
    int present;          // a physical pad has been used lately (gamepadsPhysicalActive)
    int style;            // PADSTYLE_*
    int buttons;          // PAD_ bits
    int lt, rt;           // 0..255
    int lx, ly, rx, ry;   // -32768..32767, y up
    int touching;         // a finger is on the touchpad
    float touchX, touchY; // 0..1 from the top left, while touching
    int gyro;             // motion reached the host in the last PADLIVE_MOTION_NS
    int64_t ageNs;        // since the last input, 0 when it is down now
} PadLive;
// A gyro sample this recent lights the gyro mark
#define PADLIVE_MOTION_NS 300000000LL

// Whether the card is up: the mode, whether a pad counts as in use, and how long ago it
// was last touched. The card is never up for a pad that is not in use, whatever the mode.
int padCardVisible(int mode, int padActive, int64_t ageNs);

// A stick's pull as 0..1 of full tilt, and the lit/unlit calls the drawing makes
float padStickMag(int x, int y);
#define PADLIVE_STICK_LIT 0.20f
#define PADLIVE_TRIGGER_LIT 12
int padStickLit(int x, int y);
int padTriggerLit(int v);

// The live state quantised to what the eye can see (sticks and triggers in sixteenths,
// the touch point in thirty-seconds), so the panel is redrawn on a change you could
// see and not on every wobble of a stick. Same input, same bytes; for the panel's key.
void padLiveQuantise(const PadLive* in, PadLive* out);

// Where the card hangs, relative to the head: yawRad is the head's heading (0 faces -z,
// positive turns left). The card sits PADCARD_DOWN_M under the eyes and PADCARD_FWD_M ahead
// along that heading, leaning back so its face looks at the eyes. pos receives the offset
// from the head (x, y, z), tiltRad the lean about the card's x axis (negative: its top
// leans away from you).
#define PADCARD_DOWN_M 0.45f
#define PADCARD_FWD_M 0.35f
void padCardLap(float yawRad, float pos[3], float* tiltRad);

#endif
