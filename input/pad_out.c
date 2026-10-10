// Gamepad mode's output, see pad_out.h
#include <string.h>

#include "Limelight.h"
#include "pad_out.h"

// The PAD_ bits and Moonlight's flags are meant to be the same numbers, which
// is why upstream passes them straight through. The table keeps the mapping
// explicit all the same, and these catch a change on either side.
_Static_assert(PADOUT_TYPE == LI_CTYPE_XBOX, "arrival type");
_Static_assert(PADOUT_CAPS == (LI_CCAP_ANALOG_TRIGGERS | LI_CCAP_RUMBLE), "arrival caps");

static const struct {
    int pad;
    int flag;
} kButtonMap[] = {
    { PAD_UP, UP_FLAG },
    { PAD_DOWN, DOWN_FLAG },
    { PAD_LEFT, LEFT_FLAG },
    { PAD_RIGHT, RIGHT_FLAG },
    { PAD_START, PLAY_FLAG },
    { PAD_BACK, BACK_FLAG },
    { PAD_LS_CLICK, LS_CLK_FLAG },
    { PAD_RS_CLICK, RS_CLK_FLAG },
    { PAD_LB, LB_FLAG },
    { PAD_RB, RB_FLAG },
    { PAD_A, A_FLAG },
    { PAD_B, B_FLAG },
    { PAD_X, X_FLAG },
    { PAD_Y, Y_FLAG },
};
#define BUTTON_COUNT ((int)(sizeof(kButtonMap) / sizeof(kButtonMap[0])))

int padOutButtonFlags(int padButtons) {
    int flags = 0;
    for (int i = 0; i < BUTTON_COUNT; i++) {
        if (padButtons & kButtonMap[i].pad) {
            flags |= kButtonMap[i].flag;
        }
    }
    return flags;
}

int padOutFlipFaceFlags(int flags) {
    int kept = flags & ~(A_FLAG | B_FLAG | X_FLAG | Y_FLAG);
    if (flags & A_FLAG) {
        kept |= B_FLAG;
    }
    if (flags & B_FLAG) {
        kept |= A_FLAG;
    }
    if (flags & X_FLAG) {
        kept |= Y_FLAG;
    }
    if (flags & Y_FLAG) {
        kept |= X_FLAG;
    }
    return kept;
}

unsigned char padOutTrigger(int value) {
    return (unsigned char)(value < 0 ? 0 : (value > 255 ? 255 : value));
}

short padOutAxis(int value) {
    return (short)(value < -32768 ? -32768 : (value > 32767 ? 32767 : value));
}

void padOutConvert(const PadState* pad, int flip, PadOutPacket* out) {
    int flags = padOutButtonFlags(pad->buttons);
    out->buttonFlags = flip ? padOutFlipFaceFlags(flags) : flags;
    out->leftTrigger = padOutTrigger(pad->leftTrigger);
    out->rightTrigger = padOutTrigger(pad->rightTrigger);
    out->leftStickX = padOutAxis(pad->leftX);
    out->leftStickY = padOutAxis(pad->leftY);
    out->rightStickX = padOutAxis(pad->rightX);
    out->rightStickY = padOutAxis(pad->rightY);
}

static int packetSame(const PadOutPacket* a, const PadOutPacket* b) {
    return memcmp(a, b, sizeof(*a)) == 0;
}

void padOutInit(PadOut* p, const PadOutSink* sink) {
    memset(p, 0, sizeof(*p));
    if (sink != NULL) {
        p->sink = *sink;
    }
}

void padOutSetNumber(PadOut* p, int controllerNumber, int otherMask) {
    p->controllerNumber = controllerNumber & 15;
    p->otherMask = otherMask & 0xffff & ~(1 << p->controllerNumber);
}

void padOutSetFlipFace(PadOut* p, int flip) {
    p->flipFace = flip != 0;
}

void padOutSetKeepAlive(PadOut* p, int64_t ns) {
    p->keepAliveNs = ns > 0 ? ns : 0;
}

int padOutMask(const PadOut* p) {
    return p->otherMask | (p->plugged ? 1 << p->controllerNumber : 0);
}

// The mask in the packet's type: a bit 15 pad would otherwise read negative
static short maskShort(int mask) {
    return (short)(uint16_t)mask;
}

static int sendState(PadOut* p, const PadOutPacket* pk, int64_t nowNs) {
    if (p->sink.state != NULL) {
        p->sink.state(p->sink.user, (short)p->controllerNumber, maskShort(padOutMask(p)),
                      pk->buttonFlags, pk->leftTrigger, pk->rightTrigger, pk->leftStickX,
                      pk->leftStickY, pk->rightStickX, pk->rightStickY);
    }
    p->sent = *pk;
    p->haveSent = 1;
    p->sentNs = nowNs;
    p->states++;
    return 1;
}

static int plugIn(PadOut* p) {
    p->plugged = 1;
    p->haveSent = 0;
    if (p->sink.arrival != NULL) {
        p->sink.arrival(p->sink.user, (uint8_t)p->controllerNumber,
                        (uint16_t)padOutMask(p), PADOUT_TYPE,
                        (uint32_t)padOutButtonFlags(PAD_BUTTONS), PADOUT_CAPS);
    }
    p->arrivals++;
    return 1;
}

int padOutFrame(PadOut* p, int plugged, const PadState* pad, int64_t nowNs) {
    if (!plugged) {
        return padOutUnplug(p);
    }
    int sends = 0;
    if (!p->plugged) {
        sends += plugIn(p);
    }
    PadOutPacket pk;
    padOutConvert(pad, p->flipFace, &pk);
    int due = !p->haveSent || !packetSame(&pk, &p->sent)
            || (p->keepAliveNs > 0 && nowNs - p->sentNs >= p->keepAliveNs);
    if (due) {
        sends += sendState(p, &pk, nowNs);
    }
    return sends;
}

int padOutRest(PadOut* p, int64_t nowNs) {
    if (!p->plugged) {
        return 0;
    }
    PadState rest;
    padRest(&rest);
    return padOutFrame(p, 1, &rest, nowNs);
}

int padOutUnplug(PadOut* p) {
    if (!p->plugged) {
        return 0;
    }
    p->plugged = 0;
    p->unplugs++;
    PadOutPacket rest;
    memset(&rest, 0, sizeof(rest));
    // The mask now lacks this pad's bit, which is the removal
    int sends = sendState(p, &rest, 0);
    p->haveSent = 0;
    return sends;
}
