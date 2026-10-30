// Native (lane wave4-headinput, PLAN 6.5): the dwell click: off, charging and its fraction, the
// click, once per resting place, the tolerance that restarts it, an invalid frame, and the ring
#include "check.h"
#include "xr_dwell.h"

#define MS 1000000LL

static void testTheSetting(void) {
    CHECK(dwellMsClamp(0) == 0);
    CHECK(dwellMsClamp(-5) == 0);
    CHECK(dwellMsClamp(1) == DWELL_MS_MIN);
    CHECK(dwellMsClamp(399) == 400);
    CHECK(dwellMsClamp(800) == 800);
    CHECK(dwellMsClamp(2000) == 2000);
    CHECK(dwellMsClamp(9000) == DWELL_MS_MAX);
    CHECK(POINTER_SOURCE_CONTROLLER == 0 && POINTER_SOURCE_HEAD == 1 && POINTER_SOURCE_GAZE == 2);
}

static void testOff(void) {
    Dwell d;
    dwellReset(&d);
    float f = 5.0f;
    for (int i = 0; i < 300; i++) {
        CHECK(dwellStep(&d, 1, 0.0f, 0.0f, i * 10 * MS, 0, 1.0f, &f) == DWELL_NONE);
    }
    CHECK(f == -1.0f && !d.charging);
}

static void testCharge(void) {
    Dwell d;
    dwellReset(&d);
    float f;
    // a resting pointer charges from 0 and clicks at 800 ms, at 90 Hz
    int64_t t = 1000 * MS;
    CHECK(dwellStep(&d, 1, 10.0f, -3.0f, t, 800, 1.0f, &f) == DWELL_CHARGING);
    CHECK(f == 0.0f);
    int clicks = 0, frames = 0;
    float last = 0.0f;
    int monotonic = 1;
    for (int i = 1; i <= 200; i++) {
        int what = dwellStep(&d, 1, 10.0f + 0.3f * (i % 2), -3.0f, t + i * 11111111LL, 800, 1.0f, &f);
        if (what == DWELL_CLICK) {
            clicks++;
            if (frames == 0) {
                frames = i;
            }
            CHECK(f == 1.0f);
        }
        else if (what == DWELL_CHARGING) {
            if (f < last) monotonic = 0;
            last = f;
            CHECK(f >= 0.0f && f < 1.0f);
        }
        else {
            CHECK(f == -1.0f);
        }
    }
    CHECK(monotonic);
    // 800 ms is reached on the 73rd frame at 90 Hz (72 frames of 11111111 ns fall 8 ns short)
    CHECK(frames == 73);
    // staring on: one click only
    CHECK(clicks == 1);
    // the half-way fraction
    Dwell e;
    dwellReset(&e);
    dwellStep(&e, 1, 0, 0, 0, 1000, 1.0f, &f);
    CHECK(dwellStep(&e, 1, 0, 0, 500 * MS, 1000, 1.0f, &f) == DWELL_CHARGING);
    CHECK_NEAR(f, 0.5, 1e-6);
    // a NULL fraction is fine
    CHECK(dwellStep(&e, 1, 0, 0, 1000 * MS, 1000, 1.0f, 0) == DWELL_CLICK);
}

static void testTolerance(void) {
    Dwell d;
    dwellReset(&d);
    float f;
    dwellStep(&d, 1, 0.0f, 0.0f, 0, 600, 1.0f, &f);
    // within the degree: still the same dwell
    CHECK(dwellStep(&d, 1, 0.7f, 0.7f, 300 * MS, 600, 1.0f, &f) == DWELL_CHARGING);
    CHECK_NEAR(f, 0.5, 1e-6);
    // past it: starts again from there
    CHECK(dwellStep(&d, 1, 1.2f, 0.0f, 400 * MS, 600, 1.0f, &f) == DWELL_CHARGING);
    CHECK(f == 0.0f);
    CHECK(dwellStep(&d, 1, 1.2f, 0.0f, 900 * MS, 600, 1.0f, &f) == DWELL_CHARGING);
    CHECK(dwellStep(&d, 1, 1.2f, 0.0f, 1000 * MS, 600, 1.0f, &f) == DWELL_CLICK);
    // after the click: nothing while it stays, a new dwell once it has moved on, and a second click
    CHECK(dwellStep(&d, 1, 1.5f, 0.3f, 3000 * MS, 600, 1.0f, &f) == DWELL_NONE);
    CHECK(dwellStep(&d, 1, 3.0f, 0.0f, 3100 * MS, 600, 1.0f, &f) == DWELL_CHARGING);
    CHECK(dwellStep(&d, 1, 3.0f, 0.0f, 3700 * MS, 600, 1.0f, &f) == DWELL_CLICK);
    // a wider tolerance of the caller's choosing
    Dwell w;
    dwellReset(&w);
    dwellStep(&w, 1, 0, 0, 0, 600, 3.0f, &f);
    CHECK(dwellStep(&w, 1, 2.0f, 2.0f, 600 * MS, 600, 3.0f, &f) == DWELL_CLICK);
}

static void testInvalid(void) {
    Dwell d;
    dwellReset(&d);
    float f;
    dwellStep(&d, 1, 0, 0, 0, 600, 1.0f, &f);
    CHECK(dwellStep(&d, 1, 0, 0, 500 * MS, 600, 1.0f, &f) == DWELL_CHARGING);
    // off the picture, a press held, or the head lost: dropped, and it starts again after
    CHECK(dwellStep(&d, 0, 0, 0, 550 * MS, 600, 1.0f, &f) == DWELL_NONE && f == -1.0f);
    CHECK(dwellStep(&d, 1, 0, 0, 600 * MS, 600, 1.0f, &f) == DWELL_CHARGING && f == 0.0f);
    CHECK(dwellStep(&d, 1, 0, 0, 1199 * MS, 600, 1.0f, &f) == DWELL_CHARGING);
    CHECK(dwellStep(&d, 1, 0, 0, 1200 * MS, 600, 1.0f, &f) == DWELL_CLICK);
    // the click spent, an invalid frame forgets it too: back on the same spot, it charges again
    dwellStep(&d, 0, 0, 0, 1300 * MS, 600, 1.0f, &f);
    CHECK(dwellStep(&d, 1, 0, 0, 1400 * MS, 600, 1.0f, &f) == DWELL_CHARGING);
    // not a number
    CHECK(dwellStep(&d, 1, 0.0f / 0.0f, 0, 1500 * MS, 600, 1.0f, &f) == DWELL_NONE);
}

static void testTheRing(void) {
    CHECK(dwellRingScale(-1.0f) == 0.0f);
    CHECK(dwellRingScale(0.0f) == 3.0f);
    CHECK(dwellRingScale(0.5f) == 2.0f);
    CHECK(dwellRingScale(1.0f) == 1.0f);
    CHECK(dwellRingScale(4.0f) == 1.0f);
    CHECK(dwellRingScale(0.0f / 0.0f) == 0.0f);
}

int main(void) {
    testTheSetting();
    testOff();
    testCharge();
    testTolerance();
    testInvalid();
    testTheRing();
    return checksDone("xr_dwell");
}
