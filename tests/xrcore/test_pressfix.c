// Native (lane pointer-clicks): press stabilisation (the click goes where the
// aim was before the pull jolted it, then the cursor is live again), the
// double-click snap, and the pointer's button mapping (a thumbstick click is a
// right click in pointer mode). Native (lane double-click): the wait after a
// click, which holds the cursor on the click point for a second click
#include <math.h>
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
    r->cfg.doubleNs = 500 * MS;
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
    frame(&r, 1, 0, 561.0f, 300.0f, 187); // let go where the cursor is
    CHECK_NEAR(r.x, 560.0f, 1e-2);
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
    // between clicks the cursor waits on the click point (lane double-click)
    frame(&r, 0, 0, 809.0f, 403.0f, 165);
    CHECK_NEAR(r.x, 800.0f, 1e-2);
    frame(&r, 0, 0, 812.0f, 399.0f, 176);
    CHECK_NEAR(r.x, 800.0f, 1e-2);
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
    frame(&r, 0, 0, 510.0f, 300.0f, 154); // the release goes where the cursor is
    CHECK_NEAR(r.x, 508.0f, 1e-2);
    rigInit(&r, 0, 24.0f);
    rest(&r, 500.0f, 300.0f, 0, 110);
    frame(&r, 1, 1, 507.0f, 300.0f, 121);
    CHECK_NEAR(r.x, 507.0f, 1e-2);
    frame(&r, 0, 0, 507.0f, 300.0f, 132);
    frame(&r, 1, 1, 512.0f, 300.0f, 154);
    CHECK_NEAR(r.x, 507.0f, 1e-2);
}

// Native (lane double-click): the cases from the owner's report. Every frame
// from the first press to the second release must send the first click's
// point exactly, or the host cursor moved between the clicks.
static int sentAt(const Rig* r, float x, float y) {
    return fabsf(r->x - x) < 1e-2f && fabsf(r->y - y) < 1e-2f;
}

// A double click with 3 px of jitter (each way), the second press 400 ms after
// the first. The jitter takes the aim past the 3 px circle during the first
// click, so the cursor may move a little while it is held, but nothing moves
// between the clicks and both go down at one point.
static void testDoubleClickJitter(void) {
    Rig r;
    rigInit(&r, 1, PRESSFIX_DOUBLE_DEFAULT_PX);
    rest(&r, 800.0f, 400.0f, 0, 110);
    static const float JX[] = { 0.0f, 2.0f, -3.0f, 1.0f, 3.0f, -2.0f, -1.0f, 3.0f };
    static const float JY[] = { 0.0f, -1.0f, 2.0f, 3.0f, -3.0f, 1.0f, -3.0f, 0.5f };
    int k = 0;
    frame(&r, 1, 0, 800.0f, 400.0f, 121); // touch
    frame(&r, 1, 1, 803.0f, 401.0f, 132); // press 1, a 3 px jolt
    CHECK(sentAt(&r, 800.0f, 400.0f));
    int t = 143;
    for (; t < 220; t += 11, k++) {
        frame(&r, 1, 1, 800.0f + JX[k % 8], 400.0f + JY[k % 8], t);
        CHECK(hypotf(r.x - 800.0f, r.y - 400.0f) <= 4.3f);
    }
    frame(&r, 0, 0, 803.0f, 397.0f, t); // release 1
    float relX = r.x;
    float relY = r.y;
    CHECK(hypotf(relX - 800.0f, relY - 400.0f) <= 4.3f);
    int moved = 0;
    for (t += 11; t < 510; t += 11, k++) {
        frame(&r, t > 480, 0, 800.0f + JX[k % 8], 400.0f + JY[k % 8], t);
        moved |= !sentAt(&r, relX, relY);
    }
    CHECK(!moved);
    frame(&r, 1, 1, 803.0f, 402.0f, 532); // press 2, 400 ms after press 1
    CHECK(sentAt(&r, 800.0f, 400.0f));
    frame(&r, 1, 1, 801.0f, 397.0f, 543);
    CHECK(sentAt(&r, 800.0f, 400.0f));
    frame(&r, 0, 0, 797.0f, 402.0f, 554); // release 2
    CHECK(sentAt(&r, 800.0f, 400.0f));

    // jitter inside the 3 px circle: one point from the first press to the second release
    rigInit(&r, 1, PRESSFIX_DOUBLE_DEFAULT_PX);
    rest(&r, 800.0f, 400.0f, 0, 110);
    frame(&r, 1, 0, 800.0f, 400.0f, 121);
    frame(&r, 1, 1, 803.0f, 401.0f, 132);
    moved = !sentAt(&r, 800.0f, 400.0f);
    for (t = 143, k = 0; t < 520; t += 11, k++) {
        int held = t < 220;
        frame(&r, held || t > 480, held, 800.0f + 0.65f * JX[k % 8], 400.0f + 0.65f * JY[k % 8], t);
        moved |= !sentAt(&r, 800.0f, 400.0f);
    }
    frame(&r, 1, 1, 803.0f, 402.0f, 532);
    moved |= !sentAt(&r, 800.0f, 400.0f);
    frame(&r, 0, 0, 797.0f, 402.0f, 554);
    moved |= !sentAt(&r, 800.0f, 400.0f);
    CHECK(!moved);
}

// A double click at 450 ms with the aim drifting 20 px between the clicks
static void testDoubleClickDrift(void) {
    Rig r;
    rigInit(&r, 1, PRESSFIX_DOUBLE_DEFAULT_PX);
    rest(&r, 600.0f, 300.0f, 0, 110);
    int moved = 0;
    frame(&r, 1, 0, 600.0f, 300.0f, 121);
    frame(&r, 1, 1, 605.0f, 301.0f, 132); // press 1
    moved |= !sentAt(&r, 600.0f, 300.0f);
    frame(&r, 1, 1, 602.0f, 301.0f, 143);
    frame(&r, 0, 0, 606.0f, 302.0f, 198); // release 1
    moved |= !sentAt(&r, 600.0f, 300.0f);
    // 20 px of drift, down and to the right, by the second press
    for (int t = 209; t < 582; t += 11) {
        float d = 20.0f * (float)(t - 198) / (float)(582 - 198);
        frame(&r, t > 550, 0, 600.0f + d * 0.8f, 300.0f + d * 0.6f, t);
        moved |= !sentAt(&r, 600.0f, 300.0f);
    }
    frame(&r, 1, 1, 616.0f, 312.0f, 582); // press 2, 450 ms after press 1
    moved |= !sentAt(&r, 600.0f, 300.0f);
    frame(&r, 0, 0, 618.0f, 313.0f, 637); // release 2
    moved |= !sentAt(&r, 600.0f, 300.0f);
    CHECK(!moved);
}

// A second click 600 ms after the first is two single clicks, each where it was aimed
static void testTwoSingleClicks(void) {
    Rig r;
    rigInit(&r, 1, PRESSFIX_DOUBLE_DEFAULT_PX);
    rest(&r, 400.0f, 200.0f, 0, 110);
    frame(&r, 1, 1, 400.0f, 200.0f, 121); // press 1
    CHECK_NEAR(r.x, 400.0f, 1e-2);
    frame(&r, 0, 0, 400.0f, 200.0f, 176); // release 1
    CHECK_NEAR(r.x, 400.0f, 1e-2);
    rest(&r, 412.0f, 205.0f, 187, 600); // 13 px away: waits, at most until 621 ms
    CHECK_NEAR(r.x, 400.0f, 1e-2);
    rest(&r, 412.0f, 205.0f, 622, 710);
    CHECK_NEAR(r.x, 412.0f, 1e-2);       // the wait is over: the aim is sent
    frame(&r, 1, 0, 412.0f, 205.0f, 711);
    frame(&r, 1, 1, 412.0f, 205.0f, 721); // press 2, 600 ms after press 1
    CHECK_NEAR(r.x, 412.0f, 1e-2);
    CHECK_NEAR(r.y, 205.0f, 1e-2);
    frame(&r, 0, 0, 412.0f, 205.0f, 776);
    CHECK_NEAR(r.x, 412.0f, 1e-2);
}

// Press, 30 px of movement, release: a drag, the motion sent from the press point on
static void testDragFromPress(void) {
    Rig r;
    rigInit(&r, 1, PRESSFIX_DOUBLE_DEFAULT_PX);
    rest(&r, 300.0f, 300.0f, 0, 110);
    frame(&r, 1, 0, 300.0f, 300.0f, 121);
    frame(&r, 1, 1, 300.0f, 300.0f, 132); // press
    CHECK_NEAR(r.x, 300.0f, 1e-2);
    int live = 0;
    for (int i = 1; i <= 10; i++) {
        float x = 300.0f + 3.0f * (float)i;
        frame(&r, 1, 1, x, 300.0f, 132 + 11 * i);
        if (i >= 2) {
            live += fabsf(r.x - x) < 1e-2f; // past the 3 px circle: live
        }
    }
    CHECK(live == 9);
    frame(&r, 0, 0, 330.0f, 300.0f, 253); // release at the end of the drag
    CHECK_NEAR(r.x, 330.0f, 1e-2);
    // a drag starts no wait
    frame(&r, 0, 0, 335.0f, 300.0f, 264);
    CHECK_NEAR(r.x, 335.0f, 1e-2);
    // and a drag that is the second press of a double click goes live past 24 px
    rigInit(&r, 1, PRESSFIX_DOUBLE_DEFAULT_PX);
    rest(&r, 300.0f, 300.0f, 0, 110);
    frame(&r, 1, 1, 300.0f, 300.0f, 121);
    frame(&r, 0, 0, 300.0f, 300.0f, 176);
    frame(&r, 1, 1, 302.0f, 300.0f, 231);
    CHECK_NEAR(r.x, 300.0f, 1e-2);
    frame(&r, 1, 1, 320.0f, 300.0f, 242);
    CHECK_NEAR(r.x, 300.0f, 1e-2);
    frame(&r, 1, 1, 330.0f, 300.0f, 253);
    CHECK_NEAR(r.x, 330.0f, 1e-2);
    frame(&r, 1, 1, 340.0f, 300.0f, 264);
    CHECK_NEAR(r.x, 340.0f, 1e-2);
    frame(&r, 0, 0, 345.0f, 300.0f, 275);
    CHECK_NEAR(r.x, 340.0f, 1e-2);
}

// A single click and then a move away within 100 ms: the cursor follows at
// once, with no 500 ms stall
static void testMoveAwayPromptly(void) {
    Rig r;
    rigInit(&r, 1, PRESSFIX_DOUBLE_DEFAULT_PX);
    rest(&r, 1000.0f, 500.0f, 0, 110);
    frame(&r, 1, 0, 1000.0f, 500.0f, 121);
    frame(&r, 1, 1, 1000.0f, 500.0f, 132);
    frame(&r, 0, 0, 1000.0f, 500.0f, 187); // release
    // 1.5 px/ms: an ordinary move to the next thing
    int liveAt = -1;
    for (int t = 198; t <= 287; t += 11) {
        float x = 1000.0f + 1.5f * (float)(t - 187);
        frame(&r, 0, 0, x, 500.0f, t);
        if (fabsf(r.x - x) < 1e-2f) {
            if (liveAt < 0) liveAt = t;
        }
        else {
            CHECK(x - 1000.0f <= PRESSFIX_DOUBLE_DEFAULT_PX); // held only inside the circle
        }
    }
    CHECK(liveAt > 0 && liveAt - 187 <= 33); // the first frame past 24 px
    // and stays live
    frame(&r, 0, 0, 1200.0f, 500.0f, 298);
    CHECK_NEAR(r.x, 1200.0f, 1e-2);
    // coming back into the circle inside the time does not hold it again,
    // nor snap a press there
    rest(&r, 1010.0f, 500.0f, 309, 400);
    CHECK_NEAR(r.x, 1010.0f, 1e-2);
    frame(&r, 1, 1, 1010.0f, 500.0f, 411);
    CHECK_NEAR(r.x, 1010.0f, 1e-2);
}

// The jolt going on after the first press takes the aim past the 3 px circle,
// so the cursor moves a little during that click. It is still a click: the
// cursor waits where it was let go, and the second press goes down where the
// first one did.
static void testDoubleClickAfterJolt(void) {
    Rig r;
    rigInit(&r, 1, PRESSFIX_DOUBLE_DEFAULT_PX);
    rest(&r, 800.0f, 400.0f, 0, 110);
    frame(&r, 1, 0, 800.0f, 400.0f, 121);
    frame(&r, 1, 1, 803.0f, 400.0f, 132); // press 1
    CHECK_NEAR(r.x, 800.0f, 1e-2);
    frame(&r, 1, 1, 805.0f, 400.0f, 143); // past 3 px: live
    CHECK_NEAR(r.x, 805.0f, 1e-2);
    frame(&r, 1, 1, 806.0f, 401.0f, 154);
    frame(&r, 0, 0, 812.0f, 403.0f, 165); // release 1, jolted 6 px more
    CHECK_NEAR(r.x, 806.0f, 1e-2);
    CHECK_NEAR(r.y, 401.0f, 1e-2);
    int moved = 0;
    for (int t = 176; t < 520; t += 11) {
        frame(&r, t > 490, 0, 809.0f + (float)(t % 3), 402.0f, t);
        moved |= !sentAt(&r, 806.0f, 401.0f);
    }
    CHECK(!moved);
    frame(&r, 1, 1, 811.0f, 402.0f, 532); // press 2
    CHECK_NEAR(r.x, 800.0f, 1e-2);
    CHECK_NEAR(r.y, 400.0f, 1e-2);
    frame(&r, 0, 0, 813.0f, 404.0f, 587);
    CHECK_NEAR(r.x, 800.0f, 1e-2);
}

// A slow single click (held past the double-click time) starts no wait
static void testSlowClick(void) {
    Rig r;
    rigInit(&r, 1, PRESSFIX_DOUBLE_DEFAULT_PX);
    rest(&r, 700.0f, 350.0f, 0, 110);
    frame(&r, 1, 1, 700.0f, 350.0f, 121);
    for (int t = 132; t < 700; t += 11) frame(&r, 1, 1, 701.0f, 350.0f, t);
    frame(&r, 0, 0, 701.0f, 350.0f, 700);
    CHECK_NEAR(r.x, 700.0f, 1e-2);
    frame(&r, 0, 0, 710.0f, 350.0f, 711);
    CHECK_NEAR(r.x, 710.0f, 1e-2);
    // MLXR_DOUBLE_CLICK_MS=0 turns the wait off
    rigInit(&r, 1, PRESSFIX_DOUBLE_DEFAULT_PX);
    r.cfg.doubleNs = 0;
    rest(&r, 700.0f, 350.0f, 0, 110);
    frame(&r, 1, 1, 700.0f, 350.0f, 121);
    frame(&r, 0, 0, 700.0f, 350.0f, 176);
    frame(&r, 0, 0, 705.0f, 350.0f, 187);
    CHECK_NEAR(r.x, 705.0f, 1e-2);
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
    unsetenv("MLXR_DOUBLE_CLICK_MS");
    unsetenv("MLXR_STICK_RIGHT_CLICK");
    CHECK(pressStabiliseFromEnv() == 1);
    CHECK_NEAR(doubleClickPxFromEnv(), 24.0, 1e-6);
    CHECK_NEAR(doubleClickMsFromEnv(), 500.0, 1e-6);
    setenv("MLXR_DOUBLE_CLICK_MS", "0", 1);
    CHECK_NEAR(doubleClickMsFromEnv(), 0.0, 1e-6);
    setenv("MLXR_DOUBLE_CLICK_MS", "700", 1);
    CHECK_NEAR(doubleClickMsFromEnv(), 700.0, 1e-6);
    setenv("MLXR_DOUBLE_CLICK_MS", "-5", 1);
    CHECK_NEAR(doubleClickMsFromEnv(), 500.0, 1e-6);
    setenv("MLXR_DOUBLE_CLICK_MS", "9000", 1);
    CHECK_NEAR(doubleClickMsFromEnv(), 500.0, 1e-6);
    setenv("MLXR_DOUBLE_CLICK_MS", "ms", 1);
    CHECK_NEAR(doubleClickMsFromEnv(), 500.0, 1e-6);
    unsetenv("MLXR_DOUBLE_CLICK_MS");
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
    testDoubleClickJitter();
    testDoubleClickDrift();
    testTwoSingleClicks();
    testDragFromPress();
    testMoveAwayPromptly();
    testDoubleClickAfterJolt();
    testSlowClick();
    testButtonMapping();
    testEnv();
    return checksDone("xr_pressfix");
}
