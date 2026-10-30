// Native (lane radial-dwell): the dwell click's radial menu, table driven. Each script moves the
// pointer (degrees right and up of where it starts) at given times and runs 10 ms frames; what the
// steps come to must be the script's events, in order, at their times, and the menu must be up only
// from 75 % of a dwell. Then the segments' geometry, a drag's start and its release by the next
// dwell, and the art's frames. GPLv3.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "check.h"
#include "xr_assistclick.h"
#include "xr_radial.h"

#define MS 1000000LL
#define FRAME_MS 10

typedef struct {
    int ms;
    float right, up;
    int aimed;   // 0: the pointer lost from here
} Move;

typedef struct {
    int ms, ev, action;
} Event;

typedef struct {
    const char* name;
    int dwellMs;
    int menu;             // the menu may open (the radial style on the picture)
    Move moves[8];        // ends at ms -1
    float jitterDeg;      // a square wave of this size on both axes from jitterMs, every frame
    int jitterMs;
    int endMs;
    Event want[4];        // ends at ev RADIAL_EV_NONE
    int openFirstMs;      // the first frame the menu is up (-1: never)
} Script;

#define A 1
static const Script SCRIPTS[] = {
    { "keeping still is a left click", 600, 1, { { 0, 0, 0, A }, { -1 } }, 0, 0, 1000,
      { { 600, RADIAL_EV_ACTION, ASSIST_LEFT } }, 450 },
    { "left, held 250 ms, is a double click", 600, 1, { { 0, 0, 0, A }, { 500, -3.5f, 0, A }, { -1 } }, 0, 0, 1200,
      { { 750, RADIAL_EV_ACTION, ASSIST_DOUBLE } }, 450 },
    { "down, held, is a drag", 600, 1, { { 0, 0, 0, A }, { 500, 0, -3.5f, A }, { -1 } }, 0, 0, 1200,
      { { 750, RADIAL_EV_ACTION, ASSIST_DRAG_DOWN } }, 450 },
    { "right, held, is a right click", 600, 1, { { 0, 0, 0, A }, { 470, 3.5f, 0.6f, A }, { -1 } }, 0, 0, 1200,
      { { 720, RADIAL_EV_ACTION, ASSIST_RIGHT } }, 450 },
    { "a segment held past the dwell's end still picks", 600, 1, { { 0, 0, 0, A }, { 560, 3.5f, 0, A }, { -1 } },
      0, 0, 1200, { { 810, RADIAL_EV_ACTION, ASSIST_RIGHT } }, 450 },
    { "an overshoot inside the outer edge still picks", 600, 1, { { 0, 0, 0, A }, { 500, -6.2f, 0, A }, { -1 } },
      0, 0, 1200, { { 750, RADIAL_EV_ACTION, ASSIST_DOUBLE } }, 450 },
    { "jitter inside the dead zone picks nothing", 600, 1, { { 0, 0, 0, A }, { -1 } }, 1.6f, 460, 1000,
      { { 600, RADIAL_EV_ACTION, ASSIST_LEFT } }, 450 },
    { "up picks nothing: the dwell's end is the left click", 1200, 1, { { 0, 0, 0, A }, { 910, 0, 4.0f, A }, { -1 } },
      0, 0, 1600, { { 1200, RADIAL_EV_ACTION, ASSIST_LEFT } }, 900 },
    { "past the outer edge cancels", 600, 1, { { 0, 0, 0, A }, { 500, 7.5f, 0, A }, { -1 } }, 0, 0, 900,
      { { 500, RADIAL_EV_CANCEL, ASSIST_NONE } }, 450 },
    { "the pointer lost cancels", 600, 1, { { 0, 0, 0, A }, { 500, 0, 0, 0 }, { -1 } }, 0, 0, 900,
      { { 500, RADIAL_EV_CANCEL, ASSIST_NONE } }, 450 },
    { "no menu before 75 %: a move at half way starts a new dwell", 600, 1,
      { { 0, 0, 0, A }, { 300, -3.5f, 0, A }, { -1 } }, 0, 0, 1200, { { 900, RADIAL_EV_ACTION, ASSIST_LEFT } }, 750 },
    { "a segment left starts its hold over", 900, 1,
      { { 0, 0, 0, A }, { 700, -3.5f, 0, A }, { 800, 0, 0, A }, { 850, -3.5f, 0, A }, { -1 } }, 0, 0, 1500,
      { { 1100, RADIAL_EV_ACTION, ASSIST_DOUBLE } }, 680 },
    { "back in the middle after the dwell's end is the left click", 600, 1,
      { { 0, 0, 0, A }, { 500, 3.5f, 0, A }, { 650, 0.5f, 0, A }, { -1 } }, 0, 0, 1200,
      { { 650, RADIAL_EV_ACTION, ASSIST_LEFT } }, 450 },
    { "the edge's slack keeps a segment", 900, 1,
      { { 0, 0, 0, A }, { 700, -2.6f, 0, A }, { 800, -2.35f, 0, A }, { -1 } }, 0, 0, 1400,
      { { 950, RADIAL_EV_ACTION, ASSIST_DOUBLE } }, 680 },
    { "the simple style: a plain click, never a menu", 600, 0, { { 0, 0, 0, A }, { -1 } }, 0, 0, 1000,
      { { 600, RADIAL_EV_CLICK, ASSIST_NONE } }, -1 },
    { "the simple style ignores a move past 75 %", 600, 0, { { 0, 0, 0, A }, { 500, -3.5f, 0, A }, { -1 } }, 0, 0,
      1000, { { RADIAL_EV_NONE } }, -1 },
};

static void runScripts(void) {
    for (size_t s = 0; s < sizeof SCRIPTS / sizeof SCRIPTS[0]; s++) {
        const Script* sc = &SCRIPTS[s];
        RadialDwell r;
        Dwell d;
        radialReset(&r);
        dwellReset(&d);
        Event got[8];
        int n = 0, openFirst = -1, openEarly = 0, m = 0;
        float right = 0, up = 0;
        int aimed = 1;
        for (int ms = 0; ms <= sc->endMs; ms += FRAME_MS) {
            while (sc->moves[m].ms >= 0 && sc->moves[m].ms <= ms) {
                right = sc->moves[m].right;
                up = sc->moves[m].up;
                aimed = sc->moves[m].aimed;
                m++;
            }
            float x = right, y = up;
            if (sc->jitterDeg > 0.0f && ms >= sc->jitterMs) {
                int k = (ms / FRAME_MS) % 4;
                x += k == 0 || k == 3 ? sc->jitterDeg : -sc->jitterDeg;
                y += k < 2 ? sc->jitterDeg * 0.5f : -sc->jitterDeg * 0.5f;
            }
            int action = -1;
            RadialView v;
            int ev = radialStep(&r, &d, aimed, aimed, x, y, ms * MS, sc->dwellMs, DWELL_TOLERANCE_DEG, sc->menu,
                                &action, &v);
            if (v.open && openFirst < 0) {
                openFirst = ms;
                // the menu only in the last quarter of the dwell under it
                if ((float)(ms * MS - d.startNs) < RADIAL_OPEN_AT * sc->dwellMs * MS - 1) openEarly = 1;
            }
            if (ev != RADIAL_EV_NONE && n < 8) {
                got[n].ms = ms;
                got[n].ev = ev;
                got[n].action = ev == RADIAL_EV_ACTION ? action : ASSIST_NONE;
                n++;
            }
        }
        int ok = !openEarly && openFirst == sc->openFirstMs, w = 0;
        for (; w < 4 && sc->want[w].ev != RADIAL_EV_NONE; w++) {
            if (w >= n || got[w].ms != sc->want[w].ms || got[w].ev != sc->want[w].ev
                || got[w].action != sc->want[w].action) {
                ok = 0;
            }
        }
        if (n != w) ok = 0;
        CHECK(ok);
        if (!ok) {
            fprintf(stderr, "  %s: menu first up at %d ms (%s), events:", sc->name, openFirst,
                    openEarly ? "too early" : "in time");
            for (int i = 0; i < n; i++) fprintf(stderr, " %d ms ev %d %s", got[i].ms, got[i].ev,
                                                assistActionName(got[i].action));
            fprintf(stderr, "\n");
        }
    }
}

// Where a point is: the middle, the three segments, the gap above, past the edge; the slack
static void testSectors(void) {
    static const struct { float right, up; int current, want; } T[] = {
        { 0, 0, RADIAL_NONE, RADIAL_NONE },
        { 2.4f, 0, RADIAL_NONE, RADIAL_NONE },
        { 2.6f, 0, RADIAL_NONE, RADIAL_RIGHT },
        { -3.0f, 0.5f, RADIAL_NONE, RADIAL_LEFT },
        { -3.0f, -2.9f, RADIAL_NONE, RADIAL_LEFT },
        { 0.5f, -3.0f, RADIAL_NONE, RADIAL_DOWN },
        { 0, 3.0f, RADIAL_NONE, RADIAL_NONE },
        { 0.4f, 6.5f, RADIAL_NONE, RADIAL_NONE },
        { 6.7f, 0, RADIAL_NONE, RADIAL_RIGHT },
        { 6.8f, 0, RADIAL_NONE, RADIAL_OUT },
        { 0, 7.0f, RADIAL_NONE, RADIAL_OUT },
        { 2.3f, 0, RADIAL_RIGHT, RADIAL_RIGHT },
        { 2.1f, 0, RADIAL_RIGHT, RADIAL_NONE },
        { NAN, 0, RADIAL_NONE, RADIAL_OUT },
    };
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
        int got = radialSectorAt(T[i].right, T[i].up, T[i].current);
        CHECK(got == T[i].want);
        if (got != T[i].want) fprintf(stderr, "  (%g, %g) from %d: %d, not %d\n", T[i].right, T[i].up,
                                      T[i].current, got, T[i].want);
    }
    CHECK(radialSectorAction(RADIAL_LEFT) == ASSIST_DOUBLE && radialSectorAction(RADIAL_DOWN) == ASSIST_DRAG_DOWN);
    CHECK(radialSectorAction(RADIAL_RIGHT) == ASSIST_RIGHT && radialSectorAction(RADIAL_NONE) == ASSIST_NONE);
    CHECK(fabsf(RADIAL_CANCEL_DEG - 1.5f * RADIAL_RING_DEG) < 1e-6f && RADIAL_DEAD_DEG < RADIAL_RING_DEG);
}

// A frame of the pointer at (x, y), returning the event; the menu as a drag held leaves it (none)
static int step(RadialDwell* r, Dwell* d, int64_t* t, float x, float y, int menu, int* action) {
    *t += FRAME_MS * MS;
    return radialStep(r, d, 1, 1, x, y, *t, 600, DWELL_TOLERANCE_DEG, menu, action, NULL);
}

// A drag: picked down, then (the caller holding it) the next dwell anywhere is a plain click, which
// lets it go; staring at one spot after a choice does nothing more
static void testDrag(void) {
    RadialDwell r;
    Dwell d;
    radialReset(&r);
    dwellReset(&d);
    int64_t t = 0;
    int action = ASSIST_NONE, ev = RADIAL_EV_NONE;
    for (int i = 0; i < 50 && ev == RADIAL_EV_NONE; i++) ev = step(&r, &d, &t, 0, 0, 1, &action);
    CHECK(ev == RADIAL_EV_NONE);   // still charging
    for (int i = 0; i < 50 && ev == RADIAL_EV_NONE; i++) ev = step(&r, &d, &t, 0, -3.5f, 1, &action);
    CHECK(ev == RADIAL_EV_ACTION && action == ASSIST_DRAG_DOWN && !r.open);
    // resting where it picked: spent, nothing
    int more = 0;
    for (int i = 0; i < 200; i++) more += step(&r, &d, &t, 0, -3.5f, 0, &action) != RADIAL_EV_NONE;
    CHECK(more == 0);
    // moved on and rested (the drag held, so no menu): a plain click after the dwell
    int at = -1;
    for (int i = 0; i < 100; i++) {
        if (step(&r, &d, &t, 10.0f, -6.0f, 0, &action) == RADIAL_EV_CLICK && at < 0) at = i;
    }
    CHECK(at >= 55 && at <= 61);
    // a dwell dropped from outside takes the menu with it
    radialReset(&r);
    dwellReset(&d);
    for (int i = 0; i < 50 && !r.open; i++) step(&r, &d, &t, 20.0f, 0, 1, &action);
    CHECK(r.open);
    dwellReset(&d);
    RadialView v;
    t += FRAME_MS * MS;
    radialStep(&r, &d, 1, 1, 20.0f, 0, t, 600, DWELL_TOLERANCE_DEG, 1, &action, &v);
    CHECK(!r.open && !v.open);
}

// What the drawing shows for a state
static void testFrames(void) {
    CHECK(radialRingProgress(-1.0f) < 0.0f && radialRingProgress(0.05f) < 0.0f);
    CHECK(radialRingProgress(RADIAL_RING_DELAY) == 0.0f && radialRingProgress(1.0f) == 1.0f);
    CHECK(fabsf(radialRingProgress(0.55f) - 0.5f) < 1e-5f);
    CHECK(radialRingFrame(-1.0f) == -1 && radialRingFrame(0.0f) == 0 && radialRingFrame(1.0f) == RADIAL_RING_STEPS - 1);
    int last = -1, mono = 1;
    for (int i = 0; i <= 100; i++) {
        int f = radialRingFrame(i / 100.0f);
        if (f < last) mono = 0;
        last = f;
    }
    CHECK(mono && radialHollowFrame(1.0f) == RADIAL_HOLLOW_STEPS - 1 && radialHollowFrame(-0.5f) == -1);
    // a dropped circle: three steps of fading, then gone
    CHECK(radialFadeFrame(0.5f, 0) >= RADIAL_FRAME_FADE && radialFadeFrame(0.5f, 0) < RADIAL_FRAME_HOLLOW_DOT);
    CHECK(radialFadeFrame(0.5f, 60 * MS) == radialFadeFrame(0.5f, 0) + 1);
    CHECK(radialFadeFrame(0.5f, RADIAL_RING_FADE_NS) == -1 && radialFadeFrame(-1.0f, 0) == -1);
    CHECK(radialFadeFrame(1.0f, RADIAL_RING_FADE_NS - 1) == RADIAL_FRAME_HOLLOW_DOT - 1);
    CHECK(radialFadeLevel(0) > radialFadeLevel(1) && radialFadeLevel(2) > 0.0f && radialFadeLevel(3) == 0.0f);
    CHECK(RADIAL_RING_FRAMES <= 64);
    // the ring ends just round the dot: the click when it meets it
    CHECK(RADIAL_RING_R_END > RADIAL_DOT_R && RADIAL_RING_R_END < RADIAL_DOT_R * 1.5f && RADIAL_RING_R_START < 0.95f);
    // the menu: no frame while it is not up, the fade's three steps, then each segment's four
    RadialView v;
    memset(&v, 0, sizeof v);
    v.sector = RADIAL_NONE;
    CHECK(radialMenuFrame(&v) == -1 && radialMenuScale(&v) == 0.0f);
    v.open = 1;
    v.menu = 0.0f;
    CHECK(radialMenuFrame(&v) == 0 && fabsf(radialMenuScale(&v) - RADIAL_MENU_GROW) < 1e-6f);
    v.menu = 0.5f;
    CHECK(radialMenuFrame(&v) == 1 && radialMenuScale(&v) > RADIAL_MENU_GROW && radialMenuScale(&v) < 1.0f);
    v.menu = 1.0f;
    CHECK(radialMenuFrame(&v) == RADIAL_MENU_FADE_STEPS - 1 && radialMenuScale(&v) == 1.0f);
    v.sector = RADIAL_DOWN;
    v.hold = 0.0f;
    CHECK(radialMenuFrame(&v) == RADIAL_MENU_FADE_STEPS + RADIAL_HOLD_STEPS);
    v.hold = 0.99f;
    CHECK(radialMenuFrame(&v) == RADIAL_MENU_FADE_STEPS + 2 * RADIAL_HOLD_STEPS - 1);
    v.sector = RADIAL_RIGHT;
    v.hold = 1.0f;
    CHECK(radialMenuFrame(&v) == RADIAL_MENU_FRAMES - 1);
    // the menu is drawn outside the ring as it is when the menu comes up, at any dot size (3 x)
    CHECK(RADIAL_DEAD_DEG > 0.42f * 3.0f * RADIAL_ART_SIDE * 0.5f * RADIAL_RING_R_START);
}

int main(void) {
    runScripts();
    testSectors();
    testDrag();
    testFrames();
    return checksDone("xr_radial");
}
