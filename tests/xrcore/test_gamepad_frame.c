// The Steam Frame controllers as one pad (xr_gamepad's PadHand.frame layout, taken from the
// fork): the d-pad and Back on the left, A, B, X, Y, Start and a bumper each on the right, and
// that a hand that is not a Frame controller still maps as Touch buttons. This suite is new
// here; upstream and the fork have no test for it.
#include <string.h>

#include "check.h"
#include "xr_gamepad.h"

static PadHand idle(void) {
    PadHand h;
    memset(&h, 0, sizeof(h));
    return h;
}

static PadHand frameHand(void) {
    PadHand h = idle();
    h.frame = 1;
    return h;
}

static PadState mapped(const PadHand* left, const PadHand* right, const int grips[2], int startOk) {
    PadState s;
    padMap(left, right, grips, startOk, PAD_STICK_DEADZONE_DEFAULT, &s);
    return s;
}

static void testTheBits(void) {
    CHECK(PAD_UP == 0x0001 && PAD_DOWN == 0x0002 && PAD_LEFT == 0x0004 && PAD_RIGHT == 0x0008);
    CHECK(PAD_BACK == 0x0020);
    // Moonlight's ControllerPacket flags, so the bits go on the wire as they are
    CHECK((PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT | PAD_BACK) == 0x002f);
}

static void testTheLeftHalf(void) {
    int none[2] = { 0, 0 };
    PadHand right = frameHand();
    PadHand left = frameHand();
    PadState s = mapped(&left, &right, none, 1);
    CHECK(s.buttons == 0);

    left.dpadUp = 1;
    s = mapped(&left, &right, none, 1);
    CHECK(s.buttons == PAD_UP);
    left.dpadUp = 0;
    left.lower = 1;
    s = mapped(&left, &right, none, 1);
    CHECK(s.buttons == PAD_DOWN);
    left.lower = 0;
    left.dpadLeft = 1;
    s = mapped(&left, &right, none, 1);
    CHECK(s.buttons == PAD_LEFT);
    left.dpadLeft = 0;
    left.upper = 1;
    s = mapped(&left, &right, none, 1);
    CHECK(s.buttons == PAD_RIGHT);
    left.upper = 0;
    left.bumper = 1;
    s = mapped(&left, &right, none, 1);
    CHECK(s.buttons == PAD_LB);
    left.bumper = 0;

    // The view button is Back here, where a Touch pair's menu button is Start
    left.menu = 1;
    s = mapped(&left, &right, none, 1);
    CHECK(s.buttons == PAD_BACK);
    // Held back while the mode switch is forming
    s = mapped(&left, &right, none, 0);
    CHECK(s.buttons == 0);
    // And lower and upper are no longer X and Y
    left.menu = 0;
    left.lower = left.upper = 1;
    s = mapped(&left, &right, none, 1);
    CHECK((s.buttons & (PAD_X | PAD_Y)) == 0);
}

static void testTheRightHalf(void) {
    int none[2] = { 0, 0 };
    PadHand left = frameHand();
    PadHand right = frameHand();

    right.lower = 1;
    PadState s = mapped(&left, &right, none, 1);
    CHECK(s.buttons == PAD_A);
    right.lower = 0;
    right.upper = 1;
    s = mapped(&left, &right, none, 1);
    CHECK(s.buttons == PAD_B);
    right.upper = 0;
    right.faceX = 1;
    s = mapped(&left, &right, none, 1);
    CHECK(s.buttons == PAD_X);
    right.faceX = 0;
    right.faceY = 1;
    s = mapped(&left, &right, none, 1);
    CHECK(s.buttons == PAD_Y);
    right.faceY = 0;
    right.bumper = 1;
    s = mapped(&left, &right, none, 1);
    CHECK(s.buttons == PAD_RB);
    right.bumper = 0;
    right.start = 1;
    s = mapped(&left, &right, none, 1);
    CHECK(s.buttons == PAD_START);
    // The right menu button is Start whether or not the left one is held back
    s = mapped(&left, &right, none, 0);
    CHECK(s.buttons == PAD_START);
}

static void testEverythingAtOnce(void) {
    int grips[2] = { 1, 1 };
    PadHand left = frameHand();
    PadHand right = frameHand();
    left.dpadUp = left.dpadLeft = left.lower = left.upper = left.bumper = 1;
    left.menu = left.stickClick = 1;
    right.lower = right.upper = right.faceX = right.faceY = right.bumper = right.start = 1;
    right.stickClick = 1;
    PadState s = mapped(&left, &right, grips, 1);
    CHECK(s.buttons == PAD_BUTTONS);
}

static void testMixedHands(void) {
    int none[2] = { 0, 0 };
    // A Frame left with a Touch right: the right keeps A and B and gets no X, Y or Start
    PadHand left = frameHand();
    PadHand right = idle();
    left.upper = 1;
    right.lower = right.faceX = right.start = 1;
    PadState s = mapped(&left, &right, none, 1);
    CHECK(s.buttons == (PAD_RIGHT | PAD_A));
    // A Touch left with a Frame right: X and Y from the left's face buttons, Start from menu
    left = idle();
    right = frameHand();
    left.lower = left.upper = left.menu = 1;
    right.faceY = 1;
    s = mapped(&left, &right, none, 1);
    CHECK(s.buttons == (PAD_X | PAD_Y | PAD_START));
    // The Frame fields do nothing on a hand that is not one
    left = idle();
    left.dpadUp = left.dpadLeft = left.bumper = left.start = left.faceX = left.faceY = 1;
    s = mapped(&left, &left, none, 1);
    CHECK(s.buttons == 0);
}

int main(void) {
    testTheBits();
    testTheLeftHalf();
    testTheRightHalf();
    testEverythingAtOnce();
    testMixedHands();
    return checksDone("xr_gamepad_frame");
}
