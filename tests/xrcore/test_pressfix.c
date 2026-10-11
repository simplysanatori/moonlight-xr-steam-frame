// Native (lane pointer-clicks): press stabilisation (the click goes where the
// aim was before the pull jolted it, then the cursor is live again), the
// double-click snap, and the pointer's button mapping (a thumbstick click is a
// right click in pointer mode)
#include <stdlib.h>

#include "check.h"
#include "xr_pressfix.h"

#define W 2560
#define H 1440
#define MS 1000000LL

typedef struct {
    PressFix p;
    PressFixCfg cfg;
    float x; // what was sent, in pixels
    float y;
} Rig;

static void rigInit(Rig* r, int stabilise, float doublePx) {
    pressFixReset(&r->p);
    r->cfg.stabilise = stabilise;
    r->cfg.doublePx = doublePx;
}

static void frame(Rig* r, int touching, int down, float px, float py, int tMs) {
    float u, v;
    pressFixStep(&r->p, touching, down, px / (float)W, py / (float)H, W, H, r->cfg, tMs * MS,
                 &u, &v);
    r->x = u * (float)W;
    r->y = v * (float)H;
}

// Steady aim at (x, y) from t0 to t1 in 11 ms frames, no trigger
static void rest(Rig* r, float x, float y, int t0, int t1) {
    for (int t = t0; t <= t1; t += 11) frame(r, 0, 0, x, y, t);
}

static void testPassThrough(void) {
    Rig r;
    rigInit(&r, 1, 24.0f);
    for (int i = 0; i < 30; i++) {
        frame(&r, 0, 0, 100.0f + (float)i * 7.0f, 50.0f, i * 11);
        CHECK_NEAR(r.x, 100.0f + (float)i * 7.0f, 1e-2);
    }
}

// The pull jolts the aim 7 px; the press goes where the aim was at the touch
static void testPressStabilised(void) {
    Rig r;
    rigInit(&r, 1, 0.0f);
    rest(&r, 500.0f, 300.0f, 0, 110);
    frame(&r, 1, 0, 500.0f, 300.0f, 121); // touch
    CHECK_NEAR(r.x, 500.0f, 1e-2);
    frame(&r, 1, 0, 502.0f, 300.0f, 132);
    CHECK_NEAR(r.x, 502.0f, 1e-2);        // the cursor itself is not held
    frame(&r, 1, 0, 505.0f, 300.0f, 143);
    CHECK_NEAR(r.x, 505.0f, 1e-2);
    frame(&r, 1, 1, 507.0f, 300.0f, 154); // press
    CHECK_NEAR(r.x, 500.0f, 1e-2);
    CHECK_NEAR(r.y, 300.0f, 1e-2);
    // Motion after the press is live at once: 7 px away is past the 3 px circle
    frame(&r, 1, 1, 507.0f, 300.0f, 165);
    CHECK_NEAR(r.x, 507.0f, 1e-2);
}

// A jolt that settles inside the circle keeps the whole click on one point,
// including the release, which jolts as well
static void testClickStaysAClick(void) {
    Rig r;
    rigInit(&r, 1, 0.0f);
    rest(&r, 500.0f, 300.0f, 0, 110);
    frame(&r, 1, 0, 500.0f, 300.0f, 121);
    frame(&r, 1, 0, 501.0f, 300.0f, 132);
    frame(&r, 1, 1, 502.0f, 301.0f, 143);
    CHECK_NEAR(r.x, 500.0f, 1e-2);
    frame(&r, 1, 1, 502.0f, 301.0f, 154);
    CHECK_NEAR(r.x, 500.0f, 1e-2);
    frame(&r, 1, 0, 508.0f, 303.0f, 165); // the release jolts 8 px
    CHECK_NEAR(r.x, 500.0f, 1e-2);
    CHECK_NEAR(r.y, 300.0f, 1e-2);
    frame(&r, 0, 0, 508.0f, 303.0f, 176);
    CHECK_NEAR(r.x, 508.0f, 1e-2);        // and pointing is live again
}

// A press with no pull before it (a stick click, a button) or a slow half pull
// looks back a little way only
static void testNoTouchAndSlowPull(void) {
    Rig r;
    rigInit(&r, 1, 0.0f);
    rest(&r, 500.0f, 300.0f, 0, 110);
    frame(&r, 0, 1, 504.0f, 300.0f, 121); // sample from 25 ms before: 500
    CHECK_NEAR(r.x, 500.0f, 1e-2);
    frame(&r, 0, 0, 504.0f, 300.0f, 132);
    // A pull held for a second while aiming on is not a jolt
    rigInit(&r, 1, 0.0f);
    rest(&r, 100.0f, 100.0f, 0, 55);
    frame(&r, 1, 0, 100.0f, 100.0f, 66);
    for (int t = 77; t < 1000; t += 11) frame(&r, 1, 0, 100.0f + (float)(t - 66), 100.0f, t);
    float aim = 100.0f + (float)(1001 - 66);
    frame(&r, 1, 1, aim, 100.0f, 1001);
    CHECK(r.x > aim - 150.0f && r.x < aim);      // from about 125 ms ago, not 900 ms
    CHECK(r.x > 900.0f);
}

static void testDrag(void) {
    Rig r;
    rigInit(&r, 1, 24.0f);
    rest(&r, 500.0f, 300.0f, 0, 110);
    frame(&r, 1, 0, 500.0f, 300.0f, 121);
    frame(&r, 1, 1, 500.0f, 300.0f, 132);
    CHECK_NEAR(r.x, 500.0f, 1e-2);
    // Dragging away: not held back by more than the 3 px circle
    frame(&r, 1, 1, 502.0f, 300.0f, 143);
    CHECK_NEAR(r.x, 500.0f, 1e-2);
    frame(&r, 1, 1, 520.0f, 300.0f, 154);
    CHECK_NEAR(r.x, 520.0f, 1e-2);
    frame(&r, 1, 1, 540.0f, 300.0f, 165);
    CHECK_NEAR(r.x, 540.0f, 1e-2);
    frame(&r, 1, 1, 560.0f, 300.0f, 176);
    CHECK_NEAR(r.x, 560.0f, 1e-2);
    frame(&r, 1, 0, 561.0f, 300.0f, 187); // let go where it is
    CHECK_NEAR(r.x, 561.0f, 1e-2);
    // and a drag is not a click point for a double click
    frame(&r, 0, 0, 561.0f, 300.0f, 198);
    frame(&r, 1, 1, 563.0f, 300.0f, 209);
    CHECK(r.x > 540.0f);
}

// Two clicks, the aim jolting 5 to 9 px each time: both go at the first point
static void testDoubleClick(void) {
    Rig r;
    rigInit(&r, 1, 24.0f);
    rest(&r, 800.0f, 400.0f, 0, 110);
    frame(&r, 1, 0, 800.0f, 400.0f, 121);
    frame(&r, 1, 1, 806.0f, 402.0f, 132);
    CHECK_NEAR(r.x, 800.0f, 1e-2);
    frame(&r, 1, 1, 802.0f, 401.0f, 143);
    frame(&r, 0, 0, 809.0f, 403.0f, 154); // release
    CHECK_NEAR(r.x, 800.0f, 1e-2);
    // between clicks the cursor follows the hand
    frame(&r, 0, 0, 809.0f, 403.0f, 165);
    CHECK_NEAR(r.x, 809.0f, 1e-2);
    frame(&r, 0, 0, 812.0f, 399.0f, 176);
    // second click 100 ms after the first release, the aim 14 px off the first point
    frame(&r, 1, 0, 814.0f, 399.0f, 187);
    frame(&r, 1, 1, 814.0f, 399.0f, 198);
    CHECK_NEAR(r.x, 800.0f, 1e-2);
    CHECK_NEAR(r.y, 400.0f, 1e-2);
    frame(&r, 1, 1, 818.0f, 405.0f, 209); // still inside the 24 px circle
    CHECK_NEAR(r.x, 800.0f, 1e-2);
    frame(&r, 1, 0, 820.0f, 405.0f, 220); // release
    CHECK_NEAR(r.x, 800.0f, 1e-2);
    CHECK_NEAR(r.y, 400.0f, 1e-2);
    // a third click is snapped to the same point
    frame(&r, 0, 0, 820.0f, 405.0f, 231);
    frame(&r, 1, 1, 822.0f, 405.0f, 242);
    CHECK_NEAR(r.x, 800.0f, 1e-2);
}

static void testDoubleClickDrag(void) {
    Rig r;
    rigInit(&r, 1, 24.0f);
    rest(&r, 800.0f, 400.0f, 0, 110);
    frame(&r, 1, 1, 801.0f, 400.0f, 121);
    frame(&r, 0, 0, 801.0f, 400.0f, 132);
    frame(&r, 1, 1, 810.0f, 400.0f, 165);
    CHECK_NEAR(r.x, 800.0f, 1e-2);
    frame(&r, 1, 1, 826.0f, 400.0f, 176); // 26 px: past the circle, live
    CHECK_NEAR(r.x, 826.0f, 1e-2);
    frame(&r, 1, 1, 860.0f, 400.0f, 187);
    CHECK_NEAR(r.x, 860.0f, 1e-2);
}

static void testDoubleClickLimits(void) {
    Rig r;
    // too late (500 ms)
    rigInit(&r, 1, 24.0f);
    rest(&r, 800.0f, 400.0f, 0, 110);
    frame(&r, 1, 1, 800.0f, 400.0f, 121);
    frame(&r, 0, 0, 800.0f, 400.0f, 132);
    rest(&r, 815.0f, 400.0f, 143, 700);
    frame(&r, 1, 1, 815.0f, 400.0f, 704);
    CHECK_NEAR(r.x, 815.0f, 4.0f);
    CHECK(r.x > 810.0f);
    // too far (a different icon)
    rigInit(&r, 1, 24.0f);
    rest(&r, 800.0f, 400.0f, 0, 110);
    frame(&r, 1, 1, 800.0f, 400.0f, 121);
    frame(&r, 0, 0, 800.0f, 400.0f, 132);
    rest(&r, 850.0f, 400.0f, 143, 220);
    frame(&r, 1, 1, 850.0f, 400.0f, 231);
    CHECK_NEAR(r.x, 850.0f, 1e-2);
    // switched off: MLXR_DOUBLE_CLICK_PX=0 leaves only the stabilisation
    rigInit(&r, 1, 0.0f);
    rest(&r, 800.0f, 400.0f, 0, 110);
    frame(&r, 1, 1, 800.0f, 400.0f, 121);
    frame(&r, 0, 0, 800.0f, 400.0f, 132);
    rest(&r, 815.0f, 400.0f, 143, 220);
    frame(&r, 1, 1, 815.0f, 400.0f, 231);
    CHECK_NEAR(r.x, 815.0f, 1e-2);
}

// MLXR_PRESS_STABILISE=0: the press is sent where the aim is, the double click still snaps
static void testStabiliseOff(void) {
    Rig r;
    rigInit(&r, 0, 0.0f);
    rest(&r, 500.0f, 300.0f, 0, 110);
    frame(&r, 1, 0, 500.0f, 300.0f, 121);
    frame(&r, 1, 1, 507.0f, 300.0f, 132);
    CHECK_NEAR(r.x, 507.0f, 1e-2);
    frame(&r, 1, 1, 508.0f, 300.0f, 143);
    CHECK_NEAR(r.x, 508.0f, 1e-2);
    frame(&r, 0, 0, 510.0f, 300.0f, 154);
    CHECK_NEAR(r.x, 510.0f, 1e-2);
    rigInit(&r, 0, 24.0f);
    rest(&r, 500.0f, 300.0f, 0, 110);
    frame(&r, 1, 1, 507.0f, 300.0f, 121);
    CHECK_NEAR(r.x, 507.0f, 1e-2);
    frame(&r, 0, 0, 507.0f, 300.0f, 132);
    frame(&r, 1, 1, 512.0f, 300.0f, 154);
    CHECK_NEAR(r.x, 507.0f, 1e-2);
}

static void testButtonMapping(void) {
    PointerButtons b = { 0 };
    b.stickRight = 1;
    CHECK(pointerButtonMask(&b) == 0);
    // Either thumbstick click is a right click
    b.stickClick[0] = 1;
    CHECK(pointerButtonMask(&b) == 2);
    b.stickClick[0] = 0;
    b.stickClick[1] = 1;
    CHECK(pointerButtonMask(&b) == 2);
    // with the trigger, and A, B
    b.triggerDown[1] = 1;
    CHECK(pointerButtonMask(&b) == 3);
    b.stickClick[1] = 0;
    b.rightButton = 1;
    b.middleButton = 1;
    CHECK(pointerButtonMask(&b) == 7);
    // a trigger spent on a panel clicks nothing
    b.triggerSwallowed[1] = 1;
    CHECK(pointerButtonMask(&b) == 6);
    // gamepad mode: the sticks, A and B are the pad's
    b.stickClick[0] = 1;
    b.padMode = 1;
    CHECK(pointerButtonMask(&b) == 0);
    b.padMode = 0;
    // switched off: the stick click is not a mouse button
    b.rightButton = 0;
    b.middleButton = 0;
    b.stickRight = 0;
    CHECK(pointerButtonMask(&b) == 0);
}

static void testEnv(void) {
    unsetenv("MLXR_PRESS_STABILISE");
    unsetenv("MLXR_DOUBLE_CLICK_PX");
    unsetenv("MLXR_STICK_RIGHT_CLICK");
    CHECK(pressStabiliseFromEnv() == 1);
    CHECK_NEAR(doubleClickPxFromEnv(), PRESSFIX_DOUBLE_DEFAULT_PX, 1e-6);
    CHECK(stickRightClickFromEnv() == 1);
    setenv("MLXR_PRESS_STABILISE", "0", 1);
    setenv("MLXR_DOUBLE_CLICK_PX", "0", 1);
    setenv("MLXR_STICK_RIGHT_CLICK", "0", 1);
    CHECK(pressStabiliseFromEnv() == 0);
    CHECK_NEAR(doubleClickPxFromEnv(), 0.0, 1e-6);
    CHECK(stickRightClickFromEnv() == 0);
    setenv("MLXR_PRESS_STABILISE", "1", 1);
    setenv("MLXR_DOUBLE_CLICK_PX", "40", 1);
    CHECK(pressStabiliseFromEnv() == 1);
    CHECK_NEAR(doubleClickPxFromEnv(), 40.0, 1e-6);
    setenv("MLXR_DOUBLE_CLICK_PX", "-1", 1);
    CHECK_NEAR(doubleClickPxFromEnv(), PRESSFIX_DOUBLE_DEFAULT_PX, 1e-6);
    setenv("MLXR_DOUBLE_CLICK_PX", "x", 1);
    CHECK_NEAR(doubleClickPxFromEnv(), PRESSFIX_DOUBLE_DEFAULT_PX, 1e-6);
    setenv("MLXR_DOUBLE_CLICK_PX", "", 1);
    CHECK_NEAR(doubleClickPxFromEnv(), PRESSFIX_DOUBLE_DEFAULT_PX, 1e-6);
    unsetenv("MLXR_PRESS_STABILISE");
    unsetenv("MLXR_DOUBLE_CLICK_PX");
    unsetenv("MLXR_STICK_RIGHT_CLICK");
}

int main(void) {
    testPassThrough();
    testPressStabilised();
    testClickStaysAClick();
    testNoTouchAndSlowPull();
    testDrag();
    testDoubleClick();
    testDoubleClickDrag();
    testDoubleClickLimits();
    testStabiliseOff();
    testButtonMapping();
    testEnv();
    return checksDone("xr_pressfix");
}
