// pad_out: PAD_ bits to Limelight flags, the Frame layout through padMap, flip,
// scaling, dedup, plug and unplug. The sink records instead of sending.
#include <string.h>

#include "check.h"
#include "Limelight.h"
#include "pad_out.h"

#define MAX_EVENTS 64

typedef struct {
    int kind; // 1 arrival, 2 state
    int number;
    int mask;
    int type;
    unsigned buttons;
    int caps;
    PadOutPacket pk;
} Event;

static Event events[MAX_EVENTS];
static int eventCount;

static int recArrival(void* user, uint8_t n, uint16_t mask, uint8_t type, uint32_t buttons,
                      uint16_t caps) {
    Event* e = &events[eventCount++];
    memset(e, 0, sizeof(*e));
    e->kind = 1;
    e->number = n;
    e->mask = mask;
    e->type = type;
    e->buttons = buttons;
    e->caps = caps;
    return 0;
}

static int recState(void* user, short n, short mask, int flags, unsigned char lt,
                    unsigned char rt, short lx, short ly, short rx, short ry) {
    Event* e = &events[eventCount++];
    memset(e, 0, sizeof(*e));
    e->kind = 2;
    e->number = n;
    e->mask = (uint16_t)mask;
    e->pk.buttonFlags = flags;
    e->pk.leftTrigger = lt;
    e->pk.rightTrigger = rt;
    e->pk.leftStickX = lx;
    e->pk.leftStickY = ly;
    e->pk.rightStickX = rx;
    e->pk.rightStickY = ry;
    return 0;
}

static void newPad(PadOut* p) {
    PadOutSink sink = { recArrival, recState, NULL };
    padOutInit(p, &sink);
    eventCount = 0;
}

static PadState stateWith(int buttons) {
    PadState s;
    padRest(&s);
    s.buttons = buttons;
    return s;
}

// One frame of a pad built only from this button bit set, flags sent out
static int flagsFor(int padButtons) {
    PadOut p;
    newPad(&p);
    PadState s = stateWith(padButtons);
    padOutFrame(&p, 1, &s, 0);
    return events[eventCount - 1].pk.buttonFlags;
}

static void testButtonMap(void) {
    static const struct {
        int pad;
        int flag;
    } rows[] = {
        { PAD_UP, UP_FLAG }, { PAD_DOWN, DOWN_FLAG }, { PAD_LEFT, LEFT_FLAG },
        { PAD_RIGHT, RIGHT_FLAG }, { PAD_START, PLAY_FLAG }, { PAD_BACK, BACK_FLAG },
        { PAD_LS_CLICK, LS_CLK_FLAG }, { PAD_RS_CLICK, RS_CLK_FLAG }, { PAD_LB, LB_FLAG },
        { PAD_RB, RB_FLAG }, { PAD_A, A_FLAG }, { PAD_B, B_FLAG }, { PAD_X, X_FLAG },
        { PAD_Y, Y_FLAG },
    };
    int all = 0;
    int allFlags = 0;
    for (unsigned i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        CHECK(padOutButtonFlags(rows[i].pad) == rows[i].flag);
        CHECK(flagsFor(rows[i].pad) == rows[i].flag);
        all |= rows[i].pad;
        allFlags |= rows[i].flag;
    }
    CHECK(all == PAD_BUTTONS);
    CHECK(padOutButtonFlags(PAD_BUTTONS) == allFlags);
    // The literal Moonlight values, in case the header ever changes under us
    CHECK(allFlags == 0xf3ff);
    // No SPECIAL (guide), paddles, touchpad or misc bit ever comes out
    CHECK((allFlags & (SPECIAL_FLAG | PADDLE1_FLAG | PADDLE2_FLAG | PADDLE3_FLAG | PADDLE4_FLAG
                        | TOUCHPAD_FLAG | MISC_FLAG)) == 0);
    // Bits outside PAD_BUTTONS are dropped
    CHECK(padOutButtonFlags(0x0400 | 0x0800 | 0x4000) == X_FLAG);
    CHECK(padOutButtonFlags(0) == 0);
    // Every pair of buttons together is the union
    for (unsigned i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        for (unsigned j = 0; j < sizeof(rows) / sizeof(rows[0]); j++) {
            CHECK(padOutButtonFlags(rows[i].pad | rows[j].pad) == (rows[i].flag | rows[j].flag));
        }
    }
}

// A Frame controller with nothing pressed
static PadHand frameHand(void) {
    PadHand h;
    memset(&h, 0, sizeof(h));
    h.frame = 1;
    return h;
}

static int mapFlags(const PadHand* l, const PadHand* r, const int grips[2], int flip) {
    PadState s;
    padMap(l, r, grips, 1, PAD_STICK_DEADZONE_DEFAULT, &s);
    PadOutPacket pk;
    padOutConvert(&s, flip, &pk);
    return pk.buttonFlags;
}

// The Frame layout table of STEAM_FRAME.md, row by row, through padMap
static void testFrameLayout(void) {
    const int none[2] = { 0, 0 };
    PadHand l, r;

    // Right A/B/X/Y -> A/B/X/Y: on the right hand lower is A, upper B, X and Y
    // their own inputs
    r = frameHand(); l = frameHand(); r.lower = 1;
    CHECK(mapFlags(&l, &r, none, 0) == A_FLAG);
    r = frameHand(); r.upper = 1;
    CHECK(mapFlags(&l, &r, none, 0) == B_FLAG);
    r = frameHand(); r.faceX = 1;
    CHECK(mapFlags(&l, &r, none, 0) == X_FLAG);
    r = frameHand(); r.faceY = 1;
    CHECK(mapFlags(&l, &r, none, 0) == Y_FLAG);
    // Right menu -> Start
    r = frameHand(); r.start = 1;
    CHECK(mapFlags(&l, &r, none, 0) == PLAY_FLAG);
    // Left d-pad -> d-pad (up and left their own inputs; down and right are the
    // lower and upper buttons)
    r = frameHand(); l = frameHand(); l.dpadUp = 1;
    CHECK(mapFlags(&l, &r, none, 0) == UP_FLAG);
    l = frameHand(); l.lower = 1;
    CHECK(mapFlags(&l, &r, none, 0) == DOWN_FLAG);
    l = frameHand(); l.dpadLeft = 1;
    CHECK(mapFlags(&l, &r, none, 0) == LEFT_FLAG);
    l = frameHand(); l.upper = 1;
    CHECK(mapFlags(&l, &r, none, 0) == RIGHT_FLAG);
    // Left view -> Back (the left menu button on a Frame)
    l = frameHand(); l.menu = 1;
    CHECK(mapFlags(&l, &r, none, 0) == BACK_FLAG);
    // Bumpers -> LB / RB
    l = frameHand(); l.bumper = 1;
    CHECK(mapFlags(&l, &r, none, 0) == LB_FLAG);
    l = frameHand(); r = frameHand(); r.bumper = 1;
    CHECK(mapFlags(&l, &r, none, 0) == RB_FLAG);
    // Grips (as bumpers, the grip held past its threshold) -> LB / RB
    l = frameHand(); r = frameHand();
    { const int lg[2] = { 1, 0 }; CHECK(mapFlags(&l, &r, lg, 0) == LB_FLAG); }
    { const int rg[2] = { 0, 1 }; CHECK(mapFlags(&l, &r, rg, 0) == RB_FLAG); }
    // Stick clicks
    l.stickClick = 1;
    CHECK(mapFlags(&l, &r, none, 0) == LS_CLK_FLAG);
    l.stickClick = 0; r.stickClick = 1;
    CHECK(mapFlags(&l, &r, none, 0) == RS_CLK_FLAG);
    // Nothing held, nothing sent
    r.stickClick = 0;
    CHECK(mapFlags(&l, &r, none, 0) == 0);

    // The left menu button as Start when the hand is a Touch controller
    PadHand tl, tr;
    memset(&tl, 0, sizeof(tl)); memset(&tr, 0, sizeof(tr));
    tl.menu = 1;
    CHECK(mapFlags(&tl, &tr, none, 0) == PLAY_FLAG);
    // Touch left X and Y, right A and B
    memset(&tl, 0, sizeof(tl)); tl.lower = 1; tr.lower = 1;
    CHECK(mapFlags(&tl, &tr, none, 0) == (X_FLAG | A_FLAG));
    memset(&tl, 0, sizeof(tl)); tl.upper = 1; memset(&tr, 0, sizeof(tr)); tr.upper = 1;
    CHECK(mapFlags(&tl, &tr, none, 0) == (Y_FLAG | B_FLAG));

    // Everything at once on a Frame pad, no Touch-only buttons
    l = frameHand(); r = frameHand();
    l.dpadUp = l.dpadLeft = l.lower = l.upper = l.menu = l.bumper = l.stickClick = 1;
    r.lower = r.upper = r.faceX = r.faceY = r.start = r.bumper = r.stickClick = 1;
    CHECK(mapFlags(&l, &r, none, 0) == 0xf3ff);

    // The flip face buttons setting swaps A/B and X/Y on a Frame pad too, and
    // touches nothing else
    r = frameHand(); l = frameHand(); r.lower = 1;
    CHECK(mapFlags(&l, &r, none, 1) == B_FLAG);
    r.lower = 0; r.upper = 1;
    CHECK(mapFlags(&l, &r, none, 1) == A_FLAG);
    r.upper = 0; r.faceX = 1;
    CHECK(mapFlags(&l, &r, none, 1) == Y_FLAG);
    r.faceX = 0; r.faceY = 1;
    CHECK(mapFlags(&l, &r, none, 1) == X_FLAG);
    r.faceY = 0; r.start = 1; l.dpadUp = 1; l.menu = 1; r.bumper = 1;
    CHECK(mapFlags(&l, &r, none, 1) == (PLAY_FLAG | UP_FLAG | BACK_FLAG | RB_FLAG));
}

static void testFlip(void) {
    CHECK(padOutFlipFaceFlags(A_FLAG) == B_FLAG);
    CHECK(padOutFlipFaceFlags(B_FLAG) == A_FLAG);
    CHECK(padOutFlipFaceFlags(X_FLAG) == Y_FLAG);
    CHECK(padOutFlipFaceFlags(Y_FLAG) == X_FLAG);
    CHECK(padOutFlipFaceFlags(A_FLAG | B_FLAG) == (A_FLAG | B_FLAG));
    CHECK(padOutFlipFaceFlags(A_FLAG | Y_FLAG) == (B_FLAG | X_FLAG));
    CHECK(padOutFlipFaceFlags(A_FLAG | UP_FLAG | LB_FLAG | PLAY_FLAG) ==
          (B_FLAG | UP_FLAG | LB_FLAG | PLAY_FLAG));
    CHECK(padOutFlipFaceFlags(0xf3ff) == 0xf3ff);
    CHECK(padOutFlipFaceFlags(0) == 0);
    // Flipping twice is the identity
    for (int f = 0; f < 0x10000; f += 0x11) {
        CHECK(padOutFlipFaceFlags(padOutFlipFaceFlags(f)) == f);
    }

    // Through the module, switched on and off between frames
    PadOut p;
    newPad(&p);
    PadState s = stateWith(PAD_A | PAD_X | PAD_LB);
    padOutSetFlipFace(&p, 1);
    padOutFrame(&p, 1, &s, 0);
    CHECK(events[eventCount - 1].pk.buttonFlags == (B_FLAG | Y_FLAG | LB_FLAG));
    padOutSetFlipFace(&p, 0);
    int before = eventCount;
    padOutFrame(&p, 1, &s, 1);
    // The same pad state now reads differently on the wire, so it is sent
    CHECK(eventCount == before + 1);
    CHECK(events[eventCount - 1].pk.buttonFlags == (A_FLAG | X_FLAG | LB_FLAG));
}

static void testScaling(void) {
    CHECK(padOutTrigger(0) == 0);
    CHECK(padOutTrigger(255) == 255);
    CHECK(padOutTrigger(128) == 128);
    CHECK(padOutTrigger(256) == 255);
    CHECK(padOutTrigger(100000) == 255);
    CHECK(padOutTrigger(-1) == 0);
    CHECK(padOutTrigger(-100000) == 0);
    CHECK(padOutAxis(0) == 0);
    CHECK(padOutAxis(32766) == 32766);
    CHECK(padOutAxis(-32766) == -32766);
    CHECK(padOutAxis(32767) == 32767);
    CHECK(padOutAxis(-32768) == -32768);
    CHECK(padOutAxis(32768) == 32767);
    CHECK(padOutAxis(-32769) == -32768);
    CHECK(padOutAxis(1 << 30) == 32767);
    CHECK(padOutAxis(-(1 << 30)) == -32768);

    // From a float hand through padMap: full tilt is the 0x7FFE ControllerHandler
    // scales by, y positive up as OpenXR has it and Moonlight wants it
    PadHand l, r;
    memset(&l, 0, sizeof(l)); memset(&r, 0, sizeof(r));
    l.stickX = 1.0f; l.stickY = 1.0f; l.trigger = 1.0f;
    r.stickX = -1.0f; r.stickY = -1.0f; r.trigger = 0.5f;
    const int none[2] = { 0, 0 };
    PadState s;
    padMap(&l, &r, none, 1, PAD_STICK_DEADZONE_DEFAULT, &s);
    PadOutPacket pk;
    padOutConvert(&s, 0, &pk);
    CHECK(pk.leftStickX == 32766 && pk.leftStickY == 32766);
    CHECK(pk.rightStickX == -32766 && pk.rightStickY == -32766);
    CHECK(pk.leftTrigger == 255);
    CHECK(pk.rightTrigger == 127);
    // Past full tilt (a float a hair over 1) is held to full
    l.stickX = 1.5f; l.stickY = -3.0f; l.trigger = 7.0f;
    padMap(&l, &r, none, 1, PAD_STICK_DEADZONE_DEFAULT, &s);
    padOutConvert(&s, 0, &pk);
    CHECK(pk.leftStickX == 32766 && pk.leftStickY == -32766 && pk.leftTrigger == 255);
    // A half stick, one axis only
    memset(&l, 0, sizeof(l));
    l.stickY = 0.5f;
    padMap(&l, &r, none, 1, PAD_STICK_DEADZONE_DEFAULT, &s);
    padOutConvert(&s, 0, &pk);
    CHECK(pk.leftStickX == 0 && pk.leftStickY == 16383);
    // Inside the dead zone and a light trigger read as rest
    memset(&l, 0, sizeof(l)); memset(&r, 0, sizeof(r));
    l.stickX = 0.05f; l.trigger = 0.1f;
    padMap(&l, &r, none, 1, PAD_STICK_DEADZONE_DEFAULT, &s);
    padOutConvert(&s, 0, &pk);
    CHECK(pk.leftStickX == 0 && pk.leftTrigger == 0);
    // Axes and triggers land in the right slot of the call
    PadOut p;
    newPad(&p);
    PadState q = stateWith(0);
    q.leftTrigger = 10; q.rightTrigger = 20;
    q.leftX = 1; q.leftY = 2; q.rightX = 3; q.rightY = 4;
    padOutFrame(&p, 1, &q, 0);
    const PadOutPacket* e = &events[eventCount - 1].pk;
    CHECK(e->leftTrigger == 10 && e->rightTrigger == 20);
    CHECK(e->leftStickX == 1 && e->leftStickY == 2 && e->rightStickX == 3 && e->rightStickY == 4);
    // An out of range PadState is held to the packet's types
    q.leftTrigger = 999; q.leftX = 99999; q.rightY = -99999;
    padOutFrame(&p, 1, &q, 1);
    e = &events[eventCount - 1].pk;
    CHECK(e->leftTrigger == 255 && e->leftStickX == 32767 && e->rightStickY == -32768);
}

static void testDedupAndPlug(void) {
    PadOut p;
    newPad(&p);
    PadState rest = stateWith(0);

    // Not plugged: nothing at all, however often
    for (int i = 0; i < 5; i++) {
        CHECK(padOutFrame(&p, 0, &rest, i) == 0);
    }
    CHECK(eventCount == 0);

    // Plugging in: the arrival once, with the Xbox type, rumble and analogue
    // triggers, every button and the mask with this pad, then the first state
    // even though it is at rest
    CHECK(padOutFrame(&p, 1, &rest, 100) == 2);
    CHECK(eventCount == 2);
    CHECK(events[0].kind == 1 && events[0].number == 0 && events[0].mask == 1);
    CHECK(events[0].type == LI_CTYPE_XBOX);
    CHECK(events[0].caps == (LI_CCAP_ANALOG_TRIGGERS | LI_CCAP_RUMBLE));
    CHECK((events[0].caps & LI_CCAP_TRIGGER_RUMBLE) == 0);
    CHECK(events[0].buttons == 0xf3ff);
    CHECK(events[1].kind == 2 && events[1].number == 0 && events[1].mask == 1);
    CHECK(events[1].pk.buttonFlags == 0);
    CHECK(p.arrivals == 1 && p.states == 1);

    // Unchanged for many frames: nothing, and no more arrivals
    for (int i = 0; i < 100; i++) {
        CHECK(padOutFrame(&p, 1, &rest, 200 + i) == 0);
    }
    CHECK(eventCount == 2 && p.arrivals == 1);

    // A change in any one field sends one packet; the same again does not
    PadState s = stateWith(PAD_A);
    CHECK(padOutFrame(&p, 1, &s, 1000) == 1);
    CHECK(padOutFrame(&p, 1, &s, 1001) == 0);
    s.leftTrigger = 1;
    CHECK(padOutFrame(&p, 1, &s, 1002) == 1);
    s.rightTrigger = 1;
    CHECK(padOutFrame(&p, 1, &s, 1003) == 1);
    s.leftX = 1;
    CHECK(padOutFrame(&p, 1, &s, 1004) == 1);
    s.leftY = -1;
    CHECK(padOutFrame(&p, 1, &s, 1005) == 1);
    s.rightX = 5;
    CHECK(padOutFrame(&p, 1, &s, 1006) == 1);
    s.rightY = 5;
    CHECK(padOutFrame(&p, 1, &s, 1007) == 1);
    CHECK(padOutFrame(&p, 1, &s, 1008) == 0);
    CHECK(eventCount == 9);

    // Two pad values that convert to the same packet are one state
    PadState a = stateWith(PAD_A | 0x0400);
    PadState b = stateWith(PAD_A);
    a.leftX = 40000; b.leftX = 32767;
    PadOut q;
    newPad(&q);
    padOutFrame(&q, 1, &a, 0);
    CHECK(padOutFrame(&q, 1, &b, 1) == 0);

    // Unplugging: one last packet at rest with the mask without this pad, sent
    // even though the pad was already at rest
    newPad(&p);
    padOutFrame(&p, 1, &rest, 0);
    eventCount = 0;
    CHECK(padOutFrame(&p, 0, &s, 1) == 1);
    CHECK(eventCount == 1 && events[0].kind == 2);
    CHECK(events[0].mask == 0 && events[0].number == 0);
    CHECK(events[0].pk.buttonFlags == 0 && events[0].pk.leftTrigger == 0);
    CHECK(padOutFrame(&p, 0, &s, 2) == 0);
    CHECK(padOutUnplug(&p) == 0);
    CHECK(p.unplugs == 1);

    // Plugging in again announces again, and sends its first state even if it
    // is what the host last heard
    eventCount = 0;
    CHECK(padOutFrame(&p, 1, &rest, 3) == 2);
    CHECK(events[0].kind == 1 && events[1].kind == 2);
    CHECK(p.arrivals == 2);
}

static void testNeutralOnExit(void) {
    PadOut p;
    newPad(&p);
    PadState held = stateWith(PAD_A | PAD_LB | PAD_DOWN);
    held.leftTrigger = 255; held.rightTrigger = 200;
    held.leftX = 32766; held.leftY = -32766; held.rightX = -5; held.rightY = 9;
    padOutFrame(&p, 1, &held, 0);
    eventCount = 0;

    // Leaving gamepad mode with everything held: a neutral packet goes
    CHECK(padOutFrame(&p, 0, &held, 1) == 1);
    CHECK(eventCount == 1);
    CHECK(events[0].pk.buttonFlags == 0);
    CHECK(events[0].pk.leftTrigger == 0 && events[0].pk.rightTrigger == 0);
    CHECK(events[0].pk.leftStickX == 0 && events[0].pk.leftStickY == 0);
    CHECK(events[0].pk.rightStickX == 0 && events[0].pk.rightStickY == 0);
    CHECK(events[0].mask == 0);

    // Losing focus keeps the pad plugged in but lets go of everything at once
    newPad(&p);
    padOutFrame(&p, 1, &held, 0);
    eventCount = 0;
    CHECK(padOutRest(&p, 1) == 1);
    CHECK(events[0].kind == 2 && events[0].pk.buttonFlags == 0);
    CHECK(events[0].pk.leftTrigger == 0 && events[0].pk.leftStickX == 0);
    CHECK(events[0].mask == 1);
    CHECK(p.plugged);
    // Again: nothing
    CHECK(padOutRest(&p, 2) == 0);
    CHECK(eventCount == 1);
    // Back in focus with the pad still held: sent once more
    CHECK(padOutFrame(&p, 1, &held, 3) == 1);
    CHECK(events[1].pk.buttonFlags == (A_FLAG | LB_FLAG | DOWN_FLAG));
    // Rest on an unplugged pad does nothing
    newPad(&p);
    CHECK(padOutRest(&p, 0) == 0 && eventCount == 0);
}

static void testNumberAndMask(void) {
    PadOut p;
    newPad(&p);
    padOutSetNumber(&p, 2, 0x1);
    PadState rest = stateWith(0);
    CHECK(padOutMask(&p) == 0x1);
    padOutFrame(&p, 1, &rest, 0);
    CHECK(events[0].number == 2 && events[0].mask == 0x5);
    CHECK(events[1].number == 2 && events[1].mask == 0x5);
    CHECK(padOutMask(&p) == 0x5);
    padOutFrame(&p, 0, &rest, 1);
    CHECK(events[2].number == 2 && events[2].mask == 0x1);
    // The pad's own bit is not taken as an other pad's, bit 15 survives
    newPad(&p);
    padOutSetNumber(&p, 15, 0x8001);
    padOutFrame(&p, 1, &rest, 0);
    CHECK(events[0].number == 15 && events[0].mask == 0x8001);
    CHECK(events[1].mask == 0x8001);
    padOutFrame(&p, 0, &rest, 1);
    CHECK(events[2].mask == 0x0001);
}

static void testKeepAlive(void) {
    PadOut p;
    newPad(&p);
    PadState s = stateWith(PAD_B);
    // Off by default
    padOutFrame(&p, 1, &s, 0);
    CHECK(padOutFrame(&p, 1, &s, 10000000000LL) == 0);
    newPad(&p);
    padOutSetKeepAlive(&p, 1000000000LL);
    padOutFrame(&p, 1, &s, 0);
    CHECK(padOutFrame(&p, 1, &s, 999999999LL) == 0);
    CHECK(padOutFrame(&p, 1, &s, 1000000000LL) == 1);
    CHECK(events[eventCount - 1].pk.buttonFlags == B_FLAG);
    CHECK(padOutFrame(&p, 1, &s, 1500000000LL) == 0);
    CHECK(padOutFrame(&p, 1, &s, 2000000000LL) == 1);
    // Not while unplugged
    padOutFrame(&p, 0, &s, 2100000000LL);
    int n = eventCount;
    padOutFrame(&p, 0, &s, 9000000000LL);
    CHECK(eventCount == n);
}

int main(void) {
    testButtonMap();
    testFrameLayout();
    testFlip();
    testScaling();
    testDedupAndPlug();
    testNeutralOnExit();
    testNumberAndMask();
    testKeepAlive();
    return checksDone("input_pad_out");
}
