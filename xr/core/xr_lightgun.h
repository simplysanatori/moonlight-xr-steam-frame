// Native (lane wave2-lightgun-gyro): the lightgun pointer policy (PLAN 2.3,
// report 13). With the Gun controller model in the hand the pointer is a
// lightgun, and a lightgun wants the aim exactly as it is, a shot wherever the
// barrel points, and nothing that moves the screen by accident:
//  - pointer sleep off, so the ray never goes to sleep between shots;
//  - press stabilise, the double-click wait and the click hold off, so the
//    cursor is the aim of this frame and not the aim of a moment ago;
//  - the screen is grabbed by the grip only, never the trigger, so a shot at
//    the bar below the picture does not drag the picture around;
//  - optionally, a trigger pulled off the picture is a right click (reload);
//  - a short, strong recoil pulse through the controller's haptics on every
//    shot.
// The dot where the ray lands is never touched by the policy: it stays as the
// Ray row has it.
//
// This file is the decisions only: plain arithmetic over values handed in, no
// OpenXR calls and no clock, so the host tests reach all of it (the pattern of
// xr_pressfix). xr_input.c applies them. GPLv3.
#ifndef MLXR_XR_LIGHTGUN_H
#define MLXR_XR_LIGHTGUN_H

#include <stdint.h>

#include "xr_pressfix.h"

// The lightgun config key, in the order it lists them: auto follows the
// controller model (on exactly when it is the Gun), on and off force it
#define LIGHTGUN_AUTO 0
#define LIGHTGUN_ON 1
#define LIGHTGUN_OFF 2
#define LIGHTGUN_COUNT 3

// The pointer's host buttons as xr_renderer.h's VR_BUTTON_ bits
#define LIGHTGUN_BTN_LEFT 0x1
#define LIGHTGUN_BTN_RIGHT 0x2

// The recoil pulse: full strength and short, a few frames' worth so a runtime
// plays it, with the frequency left to the runtime
#define LIGHTGUN_RECOIL_AMPLITUDE 1.0f
#define LIGHTGUN_RECOIL_NS 40000000LL

typedef struct {
    int active;        // the lightgun policy is on
    int sleepOff;      // pointer sleep is held off
    int clicksRaw;     // press stabilise, double-click wait and click hold are held off
    int gripGrabOnly;  // the trigger never grabs the screen
    int reload;        // a trigger pulled off the picture is a right click
    int recoil;        // a shot pulses the controller
} LightgunPolicy;

// A setting out of range is auto
int lightgunSettingClamp(int setting);

// Whether the policy is on: the setting forces it, auto asks the model that is
// drawn (CONTROLLER_MODEL_ kind, controllerModelKind's answer) for the gun
int lightgunActive(int setting, int modelKind);

// Everything the policy changes, from the setting, the model that is drawn and
// the two extras. Nothing is changed (all zero) while the policy is off, so
// the reload and recoil switches mean nothing outside it.
void lightgunPolicy(int setting, int modelKind, int reloadSetting, int recoilSetting,
                    LightgunPolicy* out);

// The settings' pointer sleep as the pointer sees it: held off by the policy
int lightgunSleepOn(const LightgunPolicy* p, int sleepSetting);

// The press fix and the click hold as the pointer sees them: the policy turns
// all three off, otherwise they are left as handed in
void lightgunClickConfig(const LightgunPolicy* p, PressFixCfg* cfg, float* holdPx);

// A trigger edge that may start a grab of the screen's bar: the grip always,
// the trigger only outside the policy
int lightgunTriggerMayGrab(const LightgunPolicy* p);

// ---- off-picture trigger = right click ----------------------------------------------------------
// Latches per trigger pull: a pull that starts off the picture is a right click
// for as long as it is held, wherever the aim goes meanwhile, and a pull that
// starts on the picture stays a left click however far the aim then strays.
typedef struct {
    int prevLeft;   // the left button was asked for last frame
    int latched;    // this pull is a reload
    int edge;       // set for the one frame the reload pull began
} LightgunReload;

void lightgunReloadReset(LightgunReload* r);

// One frame of the host buttons. wanted is what the controllers ask for (the
// left bit is the trigger), hit says the aim is on the picture, offPicture
// that it is off it and on nothing of ours (the room, the halo, the bar), and
// buttons the buttons as the pointer worked them out (a press only counts on
// the picture). Returns the buttons to send: while a reload pull is held the
// left button is let go of and the right one is down. r->edge says the pull
// began this frame.
int lightgunButtons(LightgunReload* r, const LightgunPolicy* p, int wanted, int hit, int offPicture,
                    int buttons);

// ---- recoil ---------------------------------------------------------------------------------------
// Whether this frame's buttons are a new shot: the left button came down, or a
// reload pull began (r->edge). prevButtons is the frame before's.
int lightgunRecoilDue(const LightgunPolicy* p, const LightgunReload* r, int prevButtons, int buttons);

#endif
