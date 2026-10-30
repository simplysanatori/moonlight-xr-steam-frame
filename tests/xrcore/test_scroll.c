// The thumbstick as a smooth high resolution wheel (xr_scroll.h): the same
// notches a second the whole-notch scroll gave, a little every frame, the
// horizontal axis, the speed setting and the dead zone. New in this fork.
#include <math.h>
#include <stdlib.h>

#include "check.h"
#include "xr_scroll.h"

#define DEADZONE 0.30f
#define BASE 7.5f
#define DT (1.0f / 90.0f)

// One second of a stick held at (x, y), summed
static void holdFor(float x, float y, int frames, float notches, int* v, int* h, int* framesMoved) {
    ScrollAcc a;
    scrollAccReset(&a);
    *v = 0;
    *h = 0;
    *framesMoved = 0;
    for (int i = 0; i < frames; i++) {
        int dv = 0, dh = 0;
        scrollAccStep(&a, x, y, DT, notches, DEADZONE, &dv, &dh);
        *v += dv;
        *h += dh;
        if (dv != 0 || dh != 0) {
            (*framesMoved)++;
        }
    }
}

static void testRates(void) {
    int v, h, moved;
    // Full tilt: 7.5 notches a second, as before, in 120ths
    holdFor(0.0f, 1.0f, 90, BASE, &v, &h, &moved);
    CHECK(abs(v - 900) <= 1);
    CHECK(h == 0);
    // ...and it comes out nearly every frame rather than every twelfth
    CHECK(moved >= 88);
    // Halfway past the dead zone: half as fast; down is negative
    holdFor(0.0f, -0.65f, 90, BASE, &v, &h, &moved);
    CHECK(abs(v + 450) <= 1);
    // Inside the dead zone nothing
    holdFor(0.25f, 0.29f, 90, BASE, &v, &h, &moved);
    CHECK(v == 0 && h == 0 && moved == 0);
    // Sideways is the horizontal wheel, right positive
    holdFor(1.0f, 0.0f, 90, BASE, &v, &h, &moved);
    CHECK(v == 0 && abs(h - 900) <= 1);
    holdFor(-1.0f, 0.0f, 90, BASE, &v, &h, &moved);
    CHECK(abs(h + 900) <= 1);
    // Up with a little sideways drift scrolls up only
    holdFor(0.4f, 0.95f, 90, BASE, &v, &h, &moved);
    CHECK(v > 0 && h == 0);
    // A true diagonal scrolls both
    holdFor(0.8f, 0.8f, 90, BASE, &v, &h, &moved);
    CHECK(v > 0 && h > 0 && abs(v - h) <= 1);
    // Twice the speed
    holdFor(0.0f, 1.0f, 90, scrollNotchesPerSec(200, BASE), &v, &h, &moved);
    CHECK(abs(v - 1800) <= 1);
}

static void testCarry(void) {
    // The rest of a unit waits for the next frame, and is dropped on letting go
    float carry = 0.0f;
    int got = stickWind(&carry, 1.0f, 0.001f, 900.0f, DEADZONE);
    CHECK(got == 0 && carry > 0.89f && carry < 0.91f);
    got = stickWind(&carry, 1.0f, 0.001f, 900.0f, DEADZONE);
    CHECK(got == 1 && carry > 0.79f && carry < 0.81f);
    got = stickWind(&carry, 0.1f, 0.001f, 900.0f, DEADZONE);
    CHECK(got == 0 && carry == 0.0f);
    // No time, no speed or a dead zone of everything: nothing
    CHECK(stickWind(&carry, 1.0f, 0.0f, 900.0f, DEADZONE) == 0);
    CHECK(stickWind(&carry, 1.0f, 0.1f, 0.0f, DEADZONE) == 0);
    CHECK(stickWind(&carry, 1.0f, 0.1f, 900.0f, 1.0f) == 0);
    // Past full tilt counts as full tilt
    carry = 0.0f;
    CHECK(stickWind(&carry, 1.5f, 1.0f, 100.0f, DEADZONE) == 100);
}

static void testSpeed(void) {
    CHECK(scrollSpeedClamp(100) == 100);
    CHECK(scrollSpeedClamp(0) == SCROLL_SPEED_MIN);
    CHECK(scrollSpeedClamp(10000) == SCROLL_SPEED_MAX);
    CHECK_NEAR(scrollNotchesPerSec(SCROLL_SPEED_DEFAULT, BASE), BASE, 1e-6);
    CHECK_NEAR(scrollNotchesPerSec(50, BASE), BASE / 2.0f, 1e-6);
    CHECK_NEAR(scrollNotchesPerSec(1, BASE), BASE * SCROLL_SPEED_MIN / 100.0f, 1e-6);
    CHECK(SCROLL_HIRES_NOTCH == 120);
}

int main(void) {
    testRates();
    testCarry();
    testSpeed();
    return checksDone("xr_scroll");
}
