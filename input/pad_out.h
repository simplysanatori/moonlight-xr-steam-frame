// Gamepad mode's output side: the pad xr_gamepad's padMap produces, sent to
// the host as one Xbox pad through moonlight-common-c. The frame loop calls
// padOutFrame() once per input frame with the pad as it stands; this module
// converts it to Moonlight's units, sends only on a change, announces the pad
// once when it plugs in, and takes it out (or lets go of everything) so no
// button is left stuck on the host.
//
// This is the C counterpart of upstream's XrRenderer.dispatchPad() (plug, send
// on change, unplug) plus ControllerHandler.attachXrPad()/reportXrPad()/
// detachXrPad() (arrival event, the flip face buttons setting, the last packet
// at rest with the mask that no longer has the pad).
//
// Not thread safe: frame loop only. The send functions are hooks, so a test
// runs without a connection; padOutUseLimelight() (pad_out_ll.c) wires the
// real moonlight-common-c calls.
#ifndef MLXR_PAD_OUT_H
#define MLXR_PAD_OUT_H

#include <stddef.h>
#include <stdint.h>

#include "xr_gamepad.h"

// What the pad says it is on arrival: an Xbox pad with analogue triggers and
// rumble, and the buttons two controllers have for an app (PAD_BUTTONS). The
// values are LI_CTYPE_XBOX, LI_CCAP_ANALOG_TRIGGERS | LI_CCAP_RUMBLE and the
// Limelight flags of PAD_BUTTONS; pad_out.c checks them against Limelight.h.
#define PADOUT_TYPE 0x01
#define PADOUT_CAPS 0x03

// The sends, with the same arguments as LiSendControllerArrivalEvent and
// LiSendMultiControllerEvent, and the user pointer handed back
typedef struct {
    int (*arrival)(void* user, uint8_t controllerNumber, uint16_t activeGamepadMask, uint8_t type,
                   uint32_t supportedButtonFlags, uint16_t capabilities);
    int (*state)(void* user, short controllerNumber, short activeGamepadMask, int buttonFlags,
                 unsigned char leftTrigger, unsigned char rightTrigger, short leftStickX,
                 short leftStickY, short rightStickX, short rightStickY);
    void* user;
} PadOutSink;

// One packet's worth as Moonlight takes it
typedef struct {
    int buttonFlags;
    unsigned char leftTrigger;
    unsigned char rightTrigger;
    short leftStickX;
    short leftStickY;
    short rightStickX;
    short rightStickY;
} PadOutPacket;

typedef struct {
    PadOutSink sink;
    // The number the pad takes (0..15; player 1 unless a real pad is there)
    // and the bits of any other pads in the host's active mask
    int controllerNumber;
    int otherMask;
    // Swap A with B and X with Y on the way out (the app's flip face buttons
    // setting)
    int flipFace;
    // Send the unchanged state again when nothing was sent for this long while
    // plugged in, 0 for never. Neither upstream nor moonlight-qt does, and the
    // gamepad channel is reliable, so it is off.
    int64_t keepAliveNs;

    int plugged;
    int haveSent;
    PadOutPacket sent;
    int64_t sentNs;
    // Counts, for logs and tests
    long arrivals;
    long states;
    long unplugs;
} PadOut;

// The Moonlight button flags of a PAD_ bit set, nothing for bits outside
// PAD_BUTTONS
int padOutButtonFlags(int padButtons);

// A and B swapped and X and Y swapped in Moonlight flags, the rest left alone
int padOutFlipFaceFlags(int flags);

// A trigger 0..255 and a stick axis as the packet's types, held to their range
unsigned char padOutTrigger(int value);
short padOutAxis(int value);

// The pad as a packet: flags (flipped when flip is set), triggers and sticks
void padOutConvert(const PadState* pad, int flip, PadOutPacket* out);

// Unplugged, pad number 0, no other pads, flip off, no keep-alive
void padOutInit(PadOut* p, const PadOutSink* sink);

// Settings, which may change between frames (the new number takes effect the
// next time the pad plugs in)
void padOutSetNumber(PadOut* p, int controllerNumber, int otherMask);
void padOutSetFlipFace(PadOut* p, int flip);
void padOutSetKeepAlive(PadOut* p, int64_t ns);

// The active gamepad mask as it stands: this pad's bit while plugged in, plus
// the others
int padOutMask(const PadOut* p);

// One input frame. plugged says gamepad mode has the pad on the host (the mode
// is on and a controller is in a hand); pad is its state, at rest when nothing
// is read this frame (padRest). Plugging in sends the arrival event once, then
// the first state whatever it is; afterwards a state goes only when it differs
// from the last one sent (or the keep-alive is due). plugged going 0 takes the
// pad out as padOutUnplug does. Returns how many sends were made.
int padOutFrame(PadOut* p, int plugged, const PadState* pad, int64_t nowNs);

// Focus lost, or nothing readable: everything let go at once, the pad staying
// plugged in. A no-op unless plugged and something other than rest was last
// sent. Equivalent to padOutFrame(p, 1, rest, now) while plugged.
int padOutRest(PadOut* p, int64_t nowNs);

// The pad out, if it was in: a last packet at rest with the mask that no
// longer has it, which is how the host hears it unplugged. Always sent, even
// when the last state was already rest, so the host removes it.
int padOutUnplug(PadOut* p);

// Wires the sink to LiSendControllerArrivalEvent / LiSendMultiControllerEvent
// (pad_out_ll.c, library mlxr_input)
void padOutUseLimelight(PadOutSink* sink);

#endif
