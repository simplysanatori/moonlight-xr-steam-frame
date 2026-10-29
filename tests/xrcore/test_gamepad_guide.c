// Gamepad mode on the Frame controllers: the stick dead zone the launcher slider asks for, the
// grips as bumpers, nothing or the stick clicks, and the Guide button made by holding the left
// View button with the right Menu button. New in this fork; upstream has no Guide.
#include <string.h>

#include "check.h"
#include "xr_gamepad.h"

#define MS 1000000LL

static PadHand frameHand(void) {
    PadHand h;
    memset(&h, 0, sizeof(h));
    h.frame = 1;
    return h;
}

static void testDeadzoneFromLauncher(void) {
    // Never moved, or moved to compensation: Moonlight's own 7 percent
    CHECK(padDeadzonePercentFromLauncher(0) == 7);
    CHECK(padDeadzonePercentFromLauncher(-1) == 7);
    CHECK(padDeadzonePercentFromLauncher(-20) == 7);
    // Moved up: that many percent, at most the slider's 20
    CHECK(padDeadzonePercentFromLauncher(1) == 1);
    CHECK(padDeadzonePercentFromLauncher(12) == 12);
    CHECK(padDeadzonePercentFromLauncher(20) == 20);
    CHECK(padDeadzonePercentFromLauncher(500) == 20);
    // It is the radius the sticks are centred inside
    int x, y;
    padStick(0.10f, 0.0f, padDeadzoneFromPercent(padDeadzonePercentFromLauncher(0)), &x, &y);
    CHECK(x != 0);
    padStick(0.10f, 0.0f, padDeadzoneFromPercent(padDeadzonePercentFromLauncher(15)), &x, &y);
    CHECK(x == 0 && y == 0);
    padStick(0.20f, 0.0f, padDeadzoneFromPercent(padDeadzonePercentFromLauncher(15)), &x, &y);
    CHECK(x == (int)(0.20f * (float)PAD_STICK_FULL) && y == 0);
}

static void testGripModes(void) {
    PadHand left = frameHand();
    PadHand right = frameHand();
    int both[2] = { 1, 1 };
    int onlyLeft[2] = { 1, 0 };
    int none[2] = { 0, 0 };
    PadState s;
    PadState plain;
    // Bumpers, which padMap is
    padMapGrips(&left, &right, both, 1, PAD_STICK_DEADZONE_DEFAULT, PAD_GRIP_BUMPERS, &s);
    CHECK(s.buttons == (PAD_LB | PAD_RB));
    padMap(&left, &right, both, 1, PAD_STICK_DEADZONE_DEFAULT, &plain);
    CHECK(padSame(&s, &plain));
    // Nothing: the grips are not on the pad, the bumper buttons still are
    padMapGrips(&left, &right, both, 1, PAD_STICK_DEADZONE_DEFAULT, PAD_GRIP_NONE, &s);
    CHECK(s.buttons == 0);
    left.bumper = 1;
    padMapGrips(&left, &right, both, 1, PAD_STICK_DEADZONE_DEFAULT, PAD_GRIP_NONE, &s);
    CHECK(s.buttons == PAD_LB);
    left.bumper = 0;
    // Stick clicks, each grip its own side, and with the click itself no different
    padMapGrips(&left, &right, onlyLeft, 1, PAD_STICK_DEADZONE_DEFAULT, PAD_GRIP_STICK_CLICKS, &s);
    CHECK(s.buttons == PAD_LS_CLICK);
    padMapGrips(&left, &right, both, 1, PAD_STICK_DEADZONE_DEFAULT, PAD_GRIP_STICK_CLICKS, &s);
    CHECK(s.buttons == (PAD_LS_CLICK | PAD_RS_CLICK));
    right.stickClick = 1;
    padMapGrips(&left, &right, onlyLeft, 1, PAD_STICK_DEADZONE_DEFAULT, PAD_GRIP_STICK_CLICKS, &s);
    CHECK(s.buttons == (PAD_LS_CLICK | PAD_RS_CLICK));
    right.stickClick = 0;
    padMapGrips(&left, &right, none, 1, PAD_STICK_DEADZONE_DEFAULT, PAD_GRIP_STICK_CLICKS, &s);
    CHECK(s.buttons == 0);
    // The mode is held to a known one
    CHECK(padGripModeClamp(PAD_GRIP_BUMPERS) == PAD_GRIP_BUMPERS);
    CHECK(padGripModeClamp(PAD_GRIP_NONE) == PAD_GRIP_NONE);
    CHECK(padGripModeClamp(PAD_GRIP_STICK_CLICKS) == PAD_GRIP_STICK_CLICKS);
    CHECK(padGripModeClamp(-1) == PAD_GRIP_BUMPERS && padGripModeClamp(3) == PAD_GRIP_BUMPERS);
}

static PadState held(int buttons) {
    PadState s;
    padRest(&s);
    s.buttons = buttons;
    return s;
}

static void testGuideChord(void) {
    PadGuide g;
    padGuideReset(&g);
    PadState s;
    int64_t t = 1000 * MS;
    int sb = PAD_START | PAD_BACK;

    // Start and Back reach the host as they are while the hold runs
    s = held(sb);
    CHECK(padGuideStep(&g, &s, t) == 0 && s.buttons == sb);
    s = held(sb);
    CHECK(padGuideStep(&g, &s, t + 200 * MS) == 0 && s.buttons == sb);
    // At the hold it is Guide, and only Guide
    s = held(sb | PAD_A);
    CHECK(padGuideStep(&g, &s, t + PAD_GUIDE_HOLD_NS) == 1);
    CHECK(s.buttons == (PAD_GUIDE | PAD_A));
    // For as long as both are down; the one frame it fired is the only one that says so
    s = held(sb);
    CHECK(padGuideStep(&g, &s, t + 400 * MS) == 0 && s.buttons == PAD_GUIDE);
    // One let go: Guide up, the other still kept off
    s = held(PAD_BACK);
    CHECK(padGuideStep(&g, &s, t + 500 * MS) == 0 && s.buttons == 0);
    s = held(sb);
    CHECK(padGuideStep(&g, &s, t + 510 * MS) == 0 && s.buttons == PAD_GUIDE);
    // Both let go, and the buttons are Start and Back again
    s = held(0);
    CHECK(padGuideStep(&g, &s, t + 600 * MS) == 0 && s.buttons == 0);
    s = held(PAD_START);
    CHECK(padGuideStep(&g, &s, t + 700 * MS) == 0 && s.buttons == PAD_START);
    s = held(0);
    padGuideStep(&g, &s, t + 710 * MS);

    // Start alone, or Back alone, held as long as you like: never Guide
    for (int i = 0; i < 20; i++) {
        s = held(PAD_START);
        CHECK(padGuideStep(&g, &s, t + (1000 + i * 100) * MS) == 0 && s.buttons == PAD_START);
    }
    s = held(0);
    padGuideStep(&g, &s, t + 4000 * MS);

    // Both pressed but one let go early: nothing taken
    s = held(sb);
    padGuideStep(&g, &s, t + 5000 * MS);
    s = held(PAD_START);
    CHECK(padGuideStep(&g, &s, t + 5100 * MS) == 0 && s.buttons == PAD_START);
    // The other comes back in with the first still down: not a chord until all were let go
    s = held(sb);
    CHECK(padGuideStep(&g, &s, t + 5200 * MS) == 0 && s.buttons == sb);
    s = held(sb);
    CHECK(padGuideStep(&g, &s, t + 5600 * MS) == 0 && s.buttons == sb);
    s = held(0);
    padGuideStep(&g, &s, t + 5700 * MS);
    s = held(sb);
    padGuideStep(&g, &s, t + 5800 * MS);
    s = held(sb);
    CHECK(padGuideStep(&g, &s, t + 5800 * MS + PAD_GUIDE_HOLD_NS) == 1);
    CHECK(s.buttons == PAD_GUIDE);
}

static void testGuideAndDisconnect(void) {
    PadGuide g;
    padGuideReset(&g);
    PadState s;
    int64_t t = 0;
    int chord = PAD_START | PAD_BACK | PAD_LB | PAD_RB;

    // The disconnect chord, all four at once, is never the Guide: nothing is touched
    for (int i = 0; i < 40; i++) {
        s = held(chord);
        CHECK(padGuideStep(&g, &s, t + i * 20 * MS) == 0 && s.buttons == chord);
    }
    s = held(0);
    padGuideStep(&g, &s, t + 1000 * MS);

    // Start and Back first, a bumper before the hold is over: the chord is the disconnect one
    s = held(PAD_START | PAD_BACK);
    padGuideStep(&g, &s, t + 2000 * MS);
    s = held(PAD_START | PAD_BACK | PAD_LB);
    CHECK(padGuideStep(&g, &s, t + 2100 * MS) == 0 && s.buttons == (PAD_START | PAD_BACK | PAD_LB));
    s = held(chord);
    CHECK(padGuideStep(&g, &s, t + 2400 * MS) == 0 && s.buttons == chord);
    s = held(0);
    padGuideStep(&g, &s, t + 3000 * MS);

    // Guide already down when a bumper joins: Start and Back come back for the chord, and Guide
    // goes
    s = held(PAD_START | PAD_BACK);
    padGuideStep(&g, &s, t + 4000 * MS);
    s = held(PAD_START | PAD_BACK);
    CHECK(padGuideStep(&g, &s, t + 4000 * MS + PAD_GUIDE_HOLD_NS) == 1);
    CHECK(s.buttons == PAD_GUIDE);
    s = held(PAD_START | PAD_BACK | PAD_LB);
    CHECK(padGuideStep(&g, &s, t + 4400 * MS) == 0);
    CHECK(s.buttons == (PAD_START | PAD_BACK | PAD_LB));
    s = held(chord);
    CHECK(padGuideStep(&g, &s, t + 4410 * MS) == 0 && s.buttons == chord);
    // and it does not start again while any of them is held
    s = held(PAD_START | PAD_BACK);
    CHECK(padGuideStep(&g, &s, t + 5000 * MS) == 0 && s.buttons == (PAD_START | PAD_BACK));
    s = held(PAD_START | PAD_BACK);
    CHECK(padGuideStep(&g, &s, t + 6000 * MS) == 0 && s.buttons == (PAD_START | PAD_BACK));
    s = held(0);
    padGuideStep(&g, &s, t + 7000 * MS);
}

static void testGuideReset(void) {
    PadGuide g;
    padGuideReset(&g);
    PadState s = held(PAD_START | PAD_BACK);
    padGuideStep(&g, &s, 0);
    s = held(PAD_START | PAD_BACK);
    CHECK(padGuideStep(&g, &s, PAD_GUIDE_HOLD_NS) == 1);
    // A reset (pointer mode, focus lost) lets go of it; with both still down it starts over
    padGuideReset(&g);
    s = held(PAD_START | PAD_BACK);
    CHECK(padGuideStep(&g, &s, PAD_GUIDE_HOLD_NS + 20 * MS) == 0);
    CHECK(s.buttons == (PAD_START | PAD_BACK));
    s = held(PAD_START | PAD_BACK);
    CHECK(padGuideStep(&g, &s, 2 * PAD_GUIDE_HOLD_NS + 20 * MS) == 1);
    CHECK(s.buttons == PAD_GUIDE);
}

int main(void) {
    testDeadzoneFromLauncher();
    testGripModes();
    testGuideChord();
    testGuideAndDisconnect();
    testGuideReset();
    return checksDone("xr_gamepad_guide");
}
