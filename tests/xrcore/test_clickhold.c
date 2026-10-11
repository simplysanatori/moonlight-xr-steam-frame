// Click hold: the host cursor held from a press until the aim is 8 pixels
// away, and for 0.35 s after the release, so single clicks, double clicks and
// drags land the way Windows' 4 px click slop needs them to
#include <stdlib.h>

#include "check.h"
#include "xr_clickhold.h"

#define W 2560
#define H 1440
#define MS 1000000LL

typedef struct {
    ClickHold c;
    float x;       // what was sent, in pixels
    float y;
} Rig;

// One frame with the aim at (px, py) pixels, at time t ms
static void frame(Rig* r, int touching, float px, float py, int tMs, float holdPx) {
    float u, v;
    clickHoldStep(&r->c, touching, px / (float)W, py / (float)H, W, H, holdPx, tMs * MS, &u, &v);
    r->x = u * (float)W;
    r->y = v * (float)H;
}

static void testPassThrough(void) {
    Rig r;
    clickHoldReset(&r.c);
    for (int i = 0; i < 20; i++) {
        frame(&r, 0, 100.0f + (float)i * 3.0f, 200.0f, i * 11, 8.0f);
        CHECK_NEAR(r.x, 100.0f + (float)i * 3.0f, 1e-2);
    }
}

static void testSingleClick(void) {
    Rig r;
    clickHoldReset(&r.c);
    for (int i = 0; i < 5; i++) frame(&r, 0, 500.0f, 300.0f, i * 11, 8.0f);
    // The pull jolts the aim by 6 px: the host cursor does not move
    frame(&r, 1, 503.0f, 301.0f, 55, 8.0f);
    frame(&r, 1, 506.0f, 302.0f, 66, 8.0f);
    CHECK_NEAR(r.x, 500.0f, 1e-2);
    CHECK_NEAR(r.y, 300.0f, 1e-2);
    frame(&r, 1, 505.0f, 301.0f, 77, 8.0f);
    CHECK_NEAR(r.x, 500.0f, 1e-2);
    // Released: still held through the window, then the live aim again
    frame(&r, 0, 504.0f, 301.0f, 88, 8.0f);
    CHECK_NEAR(r.x, 500.0f, 1e-2);
    frame(&r, 0, 504.0f, 301.0f, 88 + 300, 8.0f);
    CHECK_NEAR(r.x, 500.0f, 1e-2);
    frame(&r, 0, 504.0f, 301.0f, 88 + 400, 8.0f);
    CHECK_NEAR(r.x, 504.0f, 1e-2);
    CHECK_NEAR(r.y, 301.0f, 1e-2);
}

static void testDoubleClick(void) {
    Rig r;
    clickHoldReset(&r.c);
    for (int i = 0; i < 5; i++) frame(&r, 0, 800.0f, 600.0f, i * 11, 8.0f);
    frame(&r, 1, 804.0f, 600.0f, 60, 8.0f);
    CHECK_NEAR(r.x, 800.0f, 1e-2);
    frame(&r, 0, 803.0f, 601.0f, 150, 8.0f); // first release
    // Second press 200 ms later, the aim 7 px off the first point
    frame(&r, 0, 806.0f, 601.0f, 250, 8.0f);
    CHECK_NEAR(r.x, 800.0f, 1e-2);
    frame(&r, 1, 807.0f, 601.0f, 350, 8.0f);
    CHECK_NEAR(r.x, 800.0f, 1e-2);
    CHECK_NEAR(r.y, 600.0f, 1e-2);
    // The window restarts from the second release
    frame(&r, 0, 807.0f, 601.0f, 420, 8.0f);
    frame(&r, 0, 807.0f, 601.0f, 420 + 300, 8.0f);
    CHECK_NEAR(r.x, 800.0f, 1e-2);
    frame(&r, 0, 807.0f, 601.0f, 420 + 360, 8.0f);
    CHECK_NEAR(r.x, 807.0f, 1e-2);
}

static void testDrag(void) {
    Rig r;
    clickHoldReset(&r.c);
    for (int i = 0; i < 5; i++) frame(&r, 0, 400.0f, 400.0f, i * 11, 8.0f);
    frame(&r, 1, 404.0f, 400.0f, 60, 8.0f);
    frame(&r, 1, 407.0f, 400.0f, 71, 8.0f); // 7 px: still held
    CHECK_NEAR(r.x, 400.0f, 1e-2);
    frame(&r, 1, 409.0f, 400.0f, 82, 8.0f); // 9 px: the drag starts, live
    CHECK_NEAR(r.x, 409.0f, 1e-2);
    frame(&r, 1, 402.0f, 400.0f, 93, 8.0f); // back inside the circle: stays live
    CHECK_NEAR(r.x, 402.0f, 1e-2);
    frame(&r, 1, 600.0f, 450.0f, 104, 8.0f);
    CHECK_NEAR(r.x, 600.0f, 1e-2);
    CHECK_NEAR(r.y, 450.0f, 1e-2);
    // Released after a drag: no window
    frame(&r, 0, 601.0f, 450.0f, 115, 8.0f);
    CHECK_NEAR(r.x, 601.0f, 1e-2);
    // The distance is in pixels, both ways: 6 px down is inside, 9 px is not
    frame(&r, 1, 601.0f, 456.0f, 200, 8.0f);
    CHECK_NEAR(r.y, 450.0f, 1e-2);
    frame(&r, 1, 601.0f, 459.5f, 211, 8.0f);
    CHECK_NEAR(r.y, 459.5f, 1e-2);
}

static void testOff(void) {
    Rig r;
    clickHoldReset(&r.c);
    for (int i = 0; i < 5; i++) frame(&r, 0, 500.0f, 300.0f, i * 11, 0.0f);
    frame(&r, 1, 503.0f, 300.0f, 60, 0.0f);
    CHECK_NEAR(r.x, 503.0f, 1e-2);
    frame(&r, 0, 510.0f, 300.0f, 71, 0.0f);
    CHECK_NEAR(r.x, 510.0f, 1e-2);
}

static void testFirstFrameAndReset(void) {
    Rig r;
    clickHoldReset(&r.c);
    // A press that begins before the ray reaches the picture holds nothing
    frame(&r, 1, 300.0f, 300.0f, 0, 8.0f);
    frame(&r, 1, 305.0f, 300.0f, 11, 8.0f);
    CHECK_NEAR(r.x, 305.0f, 1e-2);
    // A reset (hand change, a gap off the picture) drops a hold
    frame(&r, 0, 305.0f, 300.0f, 22, 8.0f);
    frame(&r, 1, 308.0f, 300.0f, 33, 8.0f);
    CHECK_NEAR(r.x, 305.0f, 1e-2);
    clickHoldReset(&r.c);
    frame(&r, 1, 400.0f, 300.0f, 44, 8.0f);
    CHECK_NEAR(r.x, 400.0f, 1e-2);
}

static void testEnv(void) {
    unsetenv("MLXR_CLICK_HOLD_PX");
    CHECK_NEAR(clickHoldPixelsFromEnv(), 8.0, 1e-6);
    setenv("MLXR_CLICK_HOLD_PX", "0", 1);
    CHECK_NEAR(clickHoldPixelsFromEnv(), 0.0, 1e-6);
    setenv("MLXR_CLICK_HOLD_PX", "12.5", 1);
    CHECK_NEAR(clickHoldPixelsFromEnv(), 12.5, 1e-6);
    setenv("MLXR_CLICK_HOLD_PX", "-3", 1);
    CHECK_NEAR(clickHoldPixelsFromEnv(), 8.0, 1e-6);
    setenv("MLXR_CLICK_HOLD_PX", "abc", 1);
    CHECK_NEAR(clickHoldPixelsFromEnv(), 8.0, 1e-6);
    setenv("MLXR_CLICK_HOLD_PX", "", 1);
    CHECK_NEAR(clickHoldPixelsFromEnv(), 8.0, 1e-6);
    unsetenv("MLXR_CLICK_HOLD_PX");
}

int main(void) {
    testPassThrough();
    testSingleClick();
    testDoubleClick();
    testDrag();
    testOff();
    testFirstFrameAndReset();
    testEnv();
    return checksDone("xr_clickhold");
}
