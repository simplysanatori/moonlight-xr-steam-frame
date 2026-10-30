// Mouse emulation on a physical gamepad (Artemis style): a Bluetooth or USB
// pad that drives the host's mouse and a few keys, for the desktop, launchers
// and menus that a game pad cannot reach. Hold Start for a second (or by
// choice Start and Back) and the pad becomes the mouse; hold it again to give
// the pad back to the game.
//
// While it is a mouse: the left stick moves the pointer through an
// acceleration curve, the right stick scrolls (the setting can swap them, or
// leave scrolling off), A, B and X are the left, right and middle buttons,
// LB and RB are the back and forward buttons (X1 and X2), Y asks for the VR
// keyboard, and the d-pad is the four arrow keys. The pad's own slot sends a
// neutral state to the game for as long as it is a mouse (the caller sends
// what padMouseStep says), so the game never sees the sticks that move the
// pointer. Everything the host has down is let go when the mode turns off and
// when focus is lost or the stream ends.
//
// Plain arithmetic over values handed in and a sink of hooks, no SDL, no
// moonlight-common-c and no clock of its own, so the host tests reach all of
// it (the pattern of xr_gamepad). The buttons are the PAD_ bits of
// xr_shared.h, which are numerically Moonlight's button flags, so a physical
// pad's packet can be handed in as it is.

#ifndef XR_PADMOUSE_H
#define XR_PADMOUSE_H

#include <stdint.h>

#include "xr_shared.h"

// What turns the mode on and off, in the order the pad_mouse_chord setting
// lists them: Start held alone for PADMOUSE_HOLD_NS, Start and Back held
// together (and nothing else) for as long, or never
#define PAD_MOUSE_CHORD_START_HOLD 0
#define PAD_MOUSE_CHORD_START_BACK 1
#define PAD_MOUSE_CHORD_OFF 2

// Which stick scrolls, in the order the pad_mouse_scroll setting lists them.
// The other stick moves the pointer, so with the left stick scrolling the right
// one points. None leaves the left stick pointing and the right one idle.
#define PAD_MOUSE_SCROLL_RIGHT 0
#define PAD_MOUSE_SCROLL_LEFT 1
#define PAD_MOUSE_SCROLL_NONE 2

// What happens to the Frame controllers' gamepad slot when a physical pad is
// used, in the order the pad_arbitration setting lists them: the Frame pad is
// retired on the physical pad's first input and the controllers stay pointers
// (the default), both pads stay, or the controllers' pointer sleeps too while a
// physical pad is in use. See input/gamepads.h.
#define PAD_ARB_POINTER 0
#define PAD_ARB_ALSO_GAMEPAD 1
#define PAD_ARB_SLEEP 2

// How long the chord is held before the mode flips
#define PADMOUSE_HOLD_NS 1000000000LL
// A stick inside this fraction of full tilt does nothing; from there to full
// tilt the curve runs from 0 to 1
#define PADMOUSE_DEADZONE 0.15f
// Pointer speed in host pixels a second: a little linear part for fine
// placement and a square for the sweep, 1800 at full tilt (a 1080p picture
// edge to edge in a little over a second)
#define PADMOUSE_SPEED_LINEAR 60.0f
#define PADMOUSE_SPEED_SQUARE 1740.0f
// Scroll speed at full tilt, in the wheel's 120 a notch (10 notches a second),
// on the same kind of curve
#define PADMOUSE_SCROLL_FULL 1200.0f
// A step after a longer pause than this (a stalled thread) counts as this long,
// so the pointer does not leap
#define PADMOUSE_DT_MAX_NS 50000000LL
// How often a caller should step while the mode is on or a chord is being held
#define PADMOUSE_TICK_MS 8

// Moonlight's mouse button numbers (Limelight.h BUTTON_*)
#define PADMOUSE_BTN_LEFT 1
#define PADMOUSE_BTN_MIDDLE 2
#define PADMOUSE_BTN_RIGHT 3
#define PADMOUSE_BTN_X1 4
#define PADMOUSE_BTN_X2 5

// Windows virtual keys of the arrows
#define PADMOUSE_VK_LEFT 0x25
#define PADMOUSE_VK_UP 0x26
#define PADMOUSE_VK_RIGHT 0x27
#define PADMOUSE_VK_DOWN 0x28

// The pad as one reading: PAD_ bits and the sticks in the packet's units
// (-32768..32767, right and up positive; the triggers do nothing here)
typedef struct {
    int buttons;
    int leftX;
    int leftY;
    int rightX;
    int rightY;
} PadMouseIn;

// Where the emulation goes. Any hook may be NULL. All are called from
// padMouseStep, so under whatever lock the caller steps with: they must not
// call back into the pad module.
typedef struct {
    // Relative motion in host pixels
    void (*move)(void* user, int dx, int dy);
    // PADMOUSE_BTN_ number, down or up
    void (*button)(void* user, int button, int down);
    // 120 a notch, up and right positive
    void (*scroll)(void* user, int amount);
    void (*hscroll)(void* user, int amount);
    // A Windows virtual key, down or up
    void (*key)(void* user, int vk, int down);
    // Y was pressed: show or hide the VR keyboard
    void (*keyboard)(void* user);
    // The mode turned on or off (for a toast; padMouseNoticeText has the words)
    void (*mode)(void* user, int on);
    void* user;
} PadMouseSink;

// What the caller does with the pad's own packet this step
typedef struct {
    // Send the pad at rest: it is a mouse (or its buttons are being let go)
    int neutral;
    // Buttons to clear from the packet when it is not neutral: the chord's, kept
    // off the game until they are let go after the mode turned off
    int strip;
} PadMouseOut;

typedef struct {
    // Settings, set with padMouseConfigure
    int chord;
    int scroll;
    int on;
    // The chord is being held, and since when
    int holding;
    int64_t holdStartNs;
    // Buttons held when the mode last flipped, ignored until let go
    int strip;
    // The mouse buttons and keys the host has down, by PadMouse bit below
    int downMask;
    // Buttons of the last step, for edges
    int prevButtons;
    int64_t lastNs;
    // Sub-pixel and sub-notch remainders carried between steps
    float remX, remY, remScroll, remHScroll;
} PadMouse;

// Off, settings from the defaults (chord Start held, right stick scrolls)
void padMouseInit(PadMouse* pm);

// The settings, held to their ranges. Turning the chord off while the mode is
// on lets the next step end it.
void padMouseConfigure(PadMouse* pm, int chord, int scroll);

// One reading. live is whether the pad is on the host (focused, streaming);
// when it is not, everything is let go and the mode is off. nowNs is a
// monotonic clock. Returns what to do with the pad's own packet.
PadMouseOut padMouseStep(PadMouse* pm, const PadMouseIn* in, int live, int64_t nowNs,
                         const PadMouseSink* sink);

// Whether the caller should step again without waiting for the pad to change:
// the mode is on (a held stick moves the pointer) or the chord is being held
// (its clock runs)
int padMouseWantsTick(const PadMouse* pm);

// Lets go of every mouse button and key the host has down, stops the mode and
// forgets the chord. The sink hears the buttons up; mode off is not announced
// (this is focus lost, the stream ending, a pad gone, not the user's choice).
void padMouseRelease(PadMouse* pm, const PadMouseSink* sink);

// The curve: pixels a second for a stick deflection 0..1 (before the dead
// zone), and the dead-zoned travel 0..1 it is made from
float padMouseTravel(float deflection);
float padMouseSpeed(float deflection);
float padMouseScrollSpeed(float deflection);

// The words for the toast
const char* padMouseNoticeText(int on, int chord);

// A setting held to its range
int padMouseChordClamp(int chord);
int padMouseScrollClamp(int scroll);
int padArbitrationClamp(int mode);

#endif
