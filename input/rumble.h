// The host's rumble, as the Steam Frame controllers' haptics. moonlight-common-c
// calls the rumble callback from its control-stream thread; the frame loop
// reads the result once per input frame as a haptic request per hand. This is
// the C counterpart of upstream's XrRenderer.setRumble() (rumbleLock,
// rumbleCount, rumbleWord) and its rumbleHanded handshake in the frame loop,
// with xr_gamepad's PadRumble doing the pulses.
//
// As upstream when the mix is PAD_RUMBLE_MIX_SPLIT: the low frequency motor drives
// the left controller and the high frequency motor the right. The default
// setting is PAD_RUMBLE_MIX_BOTH, where both controllers get the stronger of the
// two, since many games drive one motor only and the other hand would stay
// silent. The trigger motors (ConnListenerRumbleTriggers) have no motor of
// their own on a Frame controller, so each adds to the controller on its side,
// as far as it is stronger. The rumble intensity setting scales all of it. The host only says when its rumble changes, so a
// request is a short pulse (PAD_RUMBLE_PULSE_NS) armed again before it runs
// out (PAD_RUMBLE_REARM_NS) for as long as the host wants it. Every call from
// the host counts, so the same two values twice still arm a pulse at once. A
// request to stop comes when the host says 0, when the controller stops being
// the pad's (live 0: mode off, no focus, no controller in that hand) or when
// rumbleStop()/rumbleReset() clear it, so a rumble the host never ended does
// not run on.
//
// No OpenXR calls here: rumbleVibration() fills an XrHapticVibration for the
// caller to hand to xrApplyHapticFeedback, and a STOP request is for
// xrStopHapticFeedback.
#ifndef MLXR_RUMBLE_H
#define MLXR_RUMBLE_H

#include <pthread.h>
#include <stdint.h>

#include <openxr/openxr.h>

#include "xr_gamepad.h"

#define RUMBLE_LEFT 0
#define RUMBLE_RIGHT 1

// What one hand's haptics should do this frame
typedef struct {
    // PAD_RUMBLE_KEEP (nothing to do), PAD_RUMBLE_APPLY or PAD_RUMBLE_STOP
    int action;
    // 0 to 1, valid for APPLY
    float amplitude;
    // Hz, or XR_FREQUENCY_UNSPECIFIED (0) to let the runtime choose
    float frequency;
    // Nanoseconds, valid for APPLY
    int64_t durationNs;
} RumbleRequest;

// What the host asked for, all four motors, 0..65535
typedef struct {
    uint16_t low;
    uint16_t high;
    uint16_t leftTrigger;
    uint16_t rightTrigger;
} RumbleWord;

typedef struct {
    // Written by any thread under the lock: the host's last word. count rises
    // with every call, so a repeat is a new word.
    pthread_mutex_t lock;
    uint64_t count;
    uint16_t low;
    uint16_t high;
    uint16_t leftTrigger;
    uint16_t rightTrigger;
    // PAD_RUMBLE_MIX_ and the intensity in percent (rumbleSetMix)
    int mix;
    int intensity;
    // The first word the host sent since the stream began, and whether the
    // frame loop has been told of it
    RumbleWord first;
    int firstSeen;
    int firstTold;
    // Which controller number the pad has on the host; rumble for any other is
    // someone else's
    int controllerNumber;
    float frequency[2];
    // Frame loop only: the last word taken, and the pulses
    uint64_t handed;
    PadRumble pulses;
} Rumble;

void rumbleInit(Rumble* r);
void rumbleDestroy(Rumble* r);

// The pad's number on the host, 0 by default (any thread, before the stream)
void rumbleSetController(Rumble* r, int controllerNumber);

// How the two motors reach the two controllers (PAD_RUMBLE_MIX_BOTH or _SPLIT)
// and the scale on every level, 0 to 100 percent. The default is the upstream
// split at 100 percent; settings hand down the real choice. Takes effect from
// the host's next word. Any thread.
void rumbleSetMix(Rumble* r, int mix, int intensityPercent);

// The frequency in Hz to ask for on each hand, XR_FREQUENCY_UNSPECIFIED by
// default as upstream (frame loop, or before the stream)
void rumbleSetFrequency(Rumble* r, float leftHz, float rightHz);

// The host's rumble for a controller, each motor 0..65535. Any thread.
void rumbleOnHost(Rumble* r, unsigned short controllerNumber, unsigned short lowFreqMotor,
                  unsigned short highFreqMotor);

// The host's trigger motors for a controller, each 0..65535, which add to the
// controller on their side. Any thread.
void rumbleTriggersOnHost(Rumble* r, unsigned short controllerNumber, unsigned short leftMotor,
                          unsigned short rightMotor);

// 1 once per stream, after the first rumble word the host sent for this pad
// (either callback) that asked for any motor, with that word, so the frame loop can log that rumble
// arrives at all, whether or not anything can be felt. Frame loop only.
int rumbleTakeFirst(Rumble* r, RumbleWord* out);

// The host's rumble gone, as at the end of a stream: the next rumblePoll stops
// both hands. Any thread.
void rumbleStop(Rumble* r);

// The pad is out: forgets pulses and drops what the host asked, without a stop
// request (padOut goes with it; live 0 in rumblePoll is what stops a running
// pulse). Frame loop only.
void rumbleReset(Rumble* r);

// One input frame. live says, left then right, whether that controller is the
// pad's this frame (mode on, focused, a controller in the hand); a hand that is
// not has its running pulse stopped. out is the request for each hand. Frame
// loop only.
void rumblePoll(Rumble* r, const int live[2], int64_t nowNs, RumbleRequest out[2]);

// Fills an XrHapticVibration (type, duration, frequency, amplitude clamped to
// 0..1) from an APPLY request and returns 1; returns 0 for KEEP and STOP,
// which leave it untouched
int rumbleVibration(const RumbleRequest* req, XrHapticVibration* out);

// The one instance moonlight-common-c's callbacks reach, since they carry no
// user pointer. Initialised on first use of rumbleGlobal() or either callback.
Rumble* rumbleGlobal(void);

// For ConnListenerRumble and ConnListenerRumbleTriggers in the connection's
// callback table.
void rumbleHostCallback(unsigned short controllerNumber, unsigned short lowFreqMotor,
                        unsigned short highFreqMotor);
void rumbleTriggersCallback(uint16_t controllerNumber, uint16_t leftTriggerMotor,
                            uint16_t rightTriggerMotor);

#endif
