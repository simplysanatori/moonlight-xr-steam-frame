// Native (lane a11y-tab, a11y-2): the Accessibility tab's pure parts: the dwell row's cells, the
// eye tracking line, the pointer row's Eyes cell, the dot's size, the row the footer describes, the
// eyes pointer's fallback to the head, and the tab bar's uneven slots; (lane a11y-final) the right
// hand's chord back to the controllers, the Eyes pointer's steadying and the dwell's tolerances
#include <stdint.h>

#include "check.h"
#include "xr_access.h"
#include "xr_dwell.h"
#include "xr_layout.h"
#include "xr_shared.h"

#define MS 1000000LL

static void testDwellCells(void) {
    CHECK(ACCESS_DWELL_CELLS == COG_ACCESS_DWELL_CELLS);
    CHECK(accessDwellMs(0) == 0);
    CHECK(accessDwellMs(1) == 600 && accessDwellMs(2) == 900 && accessDwellMs(3) == 1200 && accessDwellMs(4) == 1500);
    CHECK(accessDwellMs(-1) == -1 && accessDwellMs(5) == -1);
    // every time a cell writes is one the input pass keeps as it is
    for (int c = 1; c < ACCESS_DWELL_CELLS; c++) {
        CHECK(dwellMsClamp(accessDwellMs(c)) == accessDwellMs(c));
        CHECK(accessDwellCell(accessDwellMs(c)) == c);
    }
    CHECK(accessDwellCell(0) == 0 && accessDwellCell(-3) == 0);
    // a time only config.ini can set rings no cell
    CHECK(accessDwellCell(800) == -1 && accessDwellCell(2000) == -1);
}

static void testEyesState(void) {
    // not offered
    CHECK(accessEyesState(0, 0, 0) == ACCESS_EYES_UNAVAILABLE);
    // a gaze that arrived is ready, whatever else
    CHECK(accessEyesState(1, 0, 1) == ACCESS_EYES_READY);
    CHECK(accessEyesState(1, 1, 1) == ACCESS_EYES_READY);
    CHECK(accessEyesState(0, 0, 1) == ACCESS_EYES_READY);
    // lane a11y-2: offered is always asked for; attached and waiting, or the action refused
    CHECK(accessEyesState(1, 1, 0) == ACCESS_EYES_WAITING);
    CHECK(accessEyesState(1, 0, 0) == ACCESS_EYES_NO_ACTION);
}

static void testEyesCell(void) {
    // lane a11y-2: Eyes is always on the row, and takes a press unless the runtime has no eye gaze
    CHECK(COG_ACCESS_POINTER_CELLS == 3);
    for (int st = 0; st < ACCESS_EYES_COUNT; st++) {
        CHECK(accessEyesCellLive(st, POINTER_SOURCE_CONTROLLER) == (st != ACCESS_EYES_UNAVAILABLE));
        CHECK(accessEyesCellLive(st, POINTER_SOURCE_HEAD) == (st != ACCESS_EYES_UNAVAILABLE));
        // what is set is never dimmed
        CHECK(accessEyesCellLive(st, POINTER_SOURCE_GAZE) == 1);
    }
}

// Lane a11y-2: the dot's size
static void testDotSize(void) {
    CHECK(ACCESS_DOT_STEPS == 10);
    CHECK(cogTrackSteps(COG_TAB_ACCESS, COG_ACCESS_ROW_DOT) == ACCESS_DOT_STEPS);
    CHECK(accessDotClamp(100) == 100 && accessDotClamp(10) == 50 && accessDotClamp(999) == 300);
    CHECK(accessDotClamp(130) == 130);
    // the head and the eyes: percent of today's dot; the controllers: today's dot
    CHECK(accessDotScale(100, POINTER_SOURCE_HEAD) == 1.0f);
    CHECK(accessDotScale(50, POINTER_SOURCE_HEAD) == 0.5f);
    CHECK(accessDotScale(300, POINTER_SOURCE_GAZE) == 3.0f);
    CHECK(accessDotScale(250, POINTER_SOURCE_CONTROLLER) == 1.0f);
    CHECK(accessDotScale(5000, POINTER_SOURCE_HEAD) == 3.0f);
    // each step lands on a place of the track's eleven
    for (int i = 0; i <= ACCESS_DOT_STEPS; i++) {
        int pct = ACCESS_DOT_MIN + i * ACCESS_DOT_STEP;
        CHECK_NEAR(lanePlace(pct, ACCESS_DOT_MIN, ACCESS_DOT_MAX), (float)i / ACCESS_DOT_STEPS, 1e-5);
    }
}

// Lane a11y-2: the row the footer describes, label or cells, between rows none
static void testRowAt(void) {
    for (int row = 0; row < COG_ACCESS_ROW_COUNT; row++) {
        float v = cogRowV(COG_TAB_ACCESS, row), half = cogRowHalf(COG_TAB_ACCESS);
        CHECK(accessRowAt(v) == row);
        CHECK(accessRowAt(v - half * 0.9f) == row && accessRowAt(v + half * 0.9f) == row);
    }
    CHECK(accessRowAt(0.05f) == -1);
    CHECK(accessRowAt(COG_ACCESS_STATUS_V) == -1 && accessRowAt(COG_ACCESS_FOOTER_T) == -1);
    CHECK(accessRowAt(-1.0f) == -1);
}

// At 90 Hz: gaze for frames [from, to) is the given value
static int run(GazeFall* g, int64_t* t, int frames, int gaze, int* switches) {
    int head = g->head, changed = 0;
    for (int i = 0; i < frames; i++) {
        *t += 11 * MS;
        head = gazeFallStep(g, gaze, *t, &changed);
        if (changed && switches) (*switches)++;
    }
    return head;
}

static void testGazeFall(void) {
    GazeFall g;
    gazeFallReset(&g);
    int64_t t = 5000 * MS;
    int sw = 0;
    // no eye gaze at all: the head, always
    CHECK(run(&g, &t, 200, -1, &sw) == 1 && sw == 0);
    // eyes tracked: still the head until 300 ms of them
    gazeFallReset(&g);
    int changed = 9;
    CHECK(gazeFallStep(&g, 1, t, &changed) == 1 && changed == 0);
    CHECK(run(&g, &t, 26, 1, NULL) == 1);       // 286 ms
    CHECK(run(&g, &t, 2, 1, &sw) == 0 && sw == 1); // past 300 ms: the eyes
    // a blink (150 ms lost) leaves the eyes pointing
    sw = 0;
    CHECK(run(&g, &t, 13, 0, &sw) == 0 && sw == 0);
    CHECK(run(&g, &t, 5, 1, &sw) == 0 && sw == 0);
    // lost for more than 200 ms: the head
    CHECK(run(&g, &t, 18, 0, NULL) == 0);        // 198 ms
    CHECK(run(&g, &t, 2, 0, &sw) == 1 && sw == 1);
    // back for less than 300 ms is not enough, and a drop restarts the count
    CHECK(run(&g, &t, 20, 1, NULL) == 1);
    CHECK(run(&g, &t, 1, 0, NULL) == 1);
    CHECK(run(&g, &t, 20, 1, NULL) == 1);
    CHECK(run(&g, &t, 10, 1, NULL) == 0);
    // the eye gaze going away entirely: the head at once
    sw = 0;
    CHECK(gazeFallStep(&g, -1, t + 11 * MS, &changed) == 1 && changed == 1);
}

static void testTabSlots(void) {
    CHECK(COG_TAB_ACCESS == 4 && COG_TAB_ABOUT == 5 && COG_TAB_COUNT == 6);
    float last = 0.0f;
    for (int t = 0; t < COG_TAB_COUNT; t++) {
        float l = cogTabLeft(t), r = cogTabRight(t);
        CHECK(l == last && r > l);
        CHECK(cogTabAt((l + r) * 0.5f) == t);
        CHECK(cogTabAt(l + 0.0001f) == t);
        CHECK(cogTabAt(r - 0.0001f) == t);
        last = r;
    }
    CHECK(last == 1.0f);
    CHECK(cogTabAt(-0.5f) == 0 && cogTabAt(1.5f) == COG_TAB_COUNT - 1);
    // Accessibility has the widest slot, for its name
    for (int t = 0; t < COG_TAB_COUNT; t++) {
        if (t != COG_TAB_ACCESS) {
            CHECK(cogTabRight(COG_TAB_ACCESS) - cogTabLeft(COG_TAB_ACCESS) > cogTabRight(t) - cogTabLeft(t));
        }
    }
    // the rows sit under the tab bar, apart, and above the status line
    for (int row = 0; row < COG_ACCESS_ROW_COUNT; row++) {
        CHECK(cogRowV(COG_TAB_ACCESS, row) - cogRowHalf(COG_TAB_ACCESS) >= COG_TAB_BAR_B);
        CHECK(cogCellHalf(COG_TAB_ACCESS) < cogRowHalf(COG_TAB_ACCESS));
        CHECK(cogTrackSteps(COG_TAB_ACCESS, row) == (row == COG_ACCESS_ROW_DOT ? ACCESS_DOT_STEPS : 0));
    }
    CHECK(cogRowV(COG_TAB_ACCESS, COG_ACCESS_ROW_COUNT - 1) + cogRowHalf(COG_TAB_ACCESS) < COG_ACCESS_STATUS_V);
    CHECK(COG_ACCESS_STATUS_V < COG_ACCESS_HELP_T && COG_ACCESS_HELP_T < COG_ACCESS_FOOTER_RULE);
    CHECK(COG_ACCESS_FOOTER_RULE < COG_ACCESS_FOOTER_T && COG_ACCESS_FOOTER_T < 0.95f);
}

// ---- lane a11y-final ---------------------------------------------------------------------------

#define DEG ((float)M_PI / 180.0f)
#define FRAME (11112000LL)   // 90 Hz, rounded up so 45 frames make the half second

// A unit direction yaw and pitch degrees off straight ahead (-z)
static void dirAt(float yaw, float pitch, float d[3]) {
    d[0] = -sinf(yaw * DEG) * cosf(pitch * DEG);
    d[1] = sinf(pitch * DEG);
    d[2] = -cosf(yaw * DEG) * cosf(pitch * DEG);
}

static float degApart(const float a[3], const float b[3]) {
    float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    d = d > 1.0f ? 1.0f : (d < -1.0f ? -1.0f : d);
    return acosf(d) / DEG;
}

// A fixed sequence of noise in [-1, 1]
static uint32_t g_seed = 12345u;
static float noise(void) {
    g_seed = g_seed * 1664525u + 1013904223u;
    return (float)((g_seed >> 8) & 0xFFFFu) / 32767.5f - 1.0f;
}

static void testDwellTolerance(void) {
    CHECK(accessDwellToleranceDeg(POINTER_SOURCE_HEAD) == DWELL_TOLERANCE_DEG);
    CHECK(accessDwellToleranceDeg(POINTER_SOURCE_CONTROLLER) == DWELL_TOLERANCE_DEG);
    CHECK(accessDwellToleranceDeg(POINTER_SOURCE_GAZE) > accessDwellToleranceDeg(POINTER_SOURCE_HEAD));
    CHECK(DWELL_TOLERANCE_EYES_DEG == 2.0f);
}

// The Eyes pointer's dot: gaze jittering by up to a degree each way round one spot (a fixation)
// moves it by less than 0.3 degrees once it has settled; a real look elsewhere moves it there
static void testGazeSteady(void) {
    GazeSteady g;
    gazeSteadyReset(&g);
    float raw[3], out[3], first[3], centre[3], settled[3];
    const float dt = 1.0f / 90.0f;
    // the first frame is the gaze itself
    dirAt(3.0f, -2.0f, raw);
    gazeSteadyStep(&g, raw, dt, first);
    CHECK(degApart(first, raw) < 1e-3f);
    // 5 s of a fixation at (10, 5) with +-1 degree of noise in yaw and pitch, every frame
    gazeSteadyReset(&g);
    dirAt(10.0f, 5.0f, centre);
    float rawMax = 0.0f, moved = 0.0f, offCentre = 0.0f;
    for (int i = 0; i < 450; i++) {
        dirAt(10.0f + noise(), 5.0f + noise(), raw);
        gazeSteadyStep(&g, raw, dt, out);
        float r = degApart(raw, centre);
        if (r > rawMax) rawMax = r;
        if (i == 45) {
            settled[0] = out[0]; settled[1] = out[1]; settled[2] = out[2];
        }
        if (i > 45) {
            float m = degApart(out, settled);
            if (m > moved) moved = m;
            float c = degApart(out, centre);
            if (c > offCentre) offCentre = c;
        }
    }
    CHECK(rawMax > 0.9f);       // the noise really was about a degree
    CHECK(moved < 0.3f);        // and the dot hardly moved after its first half second
    CHECK(offCentre < 0.8f);    // and it rests near the fixation
    // the same with the raw in out (the input pass passes one array)
    gazeSteadyReset(&g);
    dirAt(0.0f, 0.0f, raw);
    gazeSteadyStep(&g, raw, dt, raw);
    CHECK(degApart(raw, centre) > 5.0f);
    // a look 10 degrees away: the dot is there (within half a degree) in 0.3 s
    gazeSteadyReset(&g);
    for (int i = 0; i < 30; i++) {
        dirAt(noise() * 0.5f, noise() * 0.5f, raw);
        gazeSteadyStep(&g, raw, dt, out);
    }
    dirAt(10.0f, 0.0f, centre);
    int at = -1;
    for (int i = 0; i < 90; i++) {
        dirAt(10.0f + noise() * 0.5f, noise() * 0.5f, raw);
        gazeSteadyStep(&g, raw, dt, out);
        if (at < 0 && degApart(out, centre) < 0.5f) at = i;
    }
    CHECK(at >= 0 && at <= 27);
    CHECK(degApart(out, centre) < 0.6f);
    // one wild frame (a blink's last sample, a tracking glitch) does not move the dot
    float before[3] = { out[0], out[1], out[2] };
    dirAt(25.0f, 10.0f, raw);
    gazeSteadyStep(&g, raw, dt, out);
    dirAt(10.0f, 0.0f, raw);
    gazeSteadyStep(&g, raw, dt, out);
    CHECK(degApart(out, before) < 0.05f);
    // a slow look across (5 degrees a second): the dot keeps up within about a degree and a half (it holds, then catches up)
    gazeSteadyReset(&g);
    float lagMax = 0.0f;
    for (int i = 0; i < 270; i++) {
        float yaw = 5.0f * (float)i * dt;
        dirAt(yaw, 0.0f, raw);
        gazeSteadyStep(&g, raw, dt, out);
        float lag = degApart(out, raw);
        if (i > 90 && lag > lagMax) lagMax = lag;
    }
    CHECK(lagMax < 1.7f);
    // a direction that is not one is ignored, and the reset forgets
    float bad[3] = { 0.0f / 0.0f, 0.0f, -1.0f };
    gazeSteadyStep(&g, bad, dt, out);
    CHECK(isfinite(out[0]) && isfinite(out[1]) && isfinite(out[2]));
    gazeSteadyReset(&g);
    CHECK(!g.valid);
}

// Frames of the right Menu button and grip at 90 Hz from *t; how many fired, and what the bindings
// saw of Menu on the last frame and how many frames it was down
static int chord(PointerBack* b, int64_t* t, int frames, int menu, int grip, int* seen, int* downFrames) {
    int fired = 0, out = 0;
    for (int i = 0; i < frames; i++) {
        *t += FRAME;
        fired += pointerBackStep(b, menu, grip, *t, &out);
        if (downFrames != NULL && out) (*downFrames)++;
    }
    if (seen != NULL) *seen = out;
    return fired;
}

static void testPointerBack(void) {
    PointerBack b;
    int64_t t = 1000 * MS;
    int seen = 0, down = 0;
    // after a reset a Menu already down is not a press: nothing until it is let go
    pointerBackReset(&b);
    CHECK(chord(&b, &t, 60, 1, 1, &seen, &down) == 0 && down == 0);
    CHECK(chord(&b, &t, 2, 0, 0, &seen, &down) == 0 && down == 0);
    // Menu tapped alone: nothing while it is down, one frame down on the release (the cog's press)
    down = 0;
    CHECK(chord(&b, &t, 20, 1, 0, &seen, &down) == 0 && down == 0);
    CHECK(chord(&b, &t, 1, 0, 0, &seen, &down) == 0 && seen == 1 && down == 1);
    CHECK(chord(&b, &t, 3, 0, 0, &seen, &down) == 0 && seen == 0 && down == 1);
    // Menu and the grip together for half a second: the chord fires once, Menu never goes through
    down = 0;
    CHECK(chord(&b, &t, 44, 1, 1, &seen, &down) == 0);
    CHECK(chord(&b, &t, 2, 1, 1, &seen, &down) == 1);
    CHECK(chord(&b, &t, 200, 1, 1, &seen, &down) == 0 && down == 0);
    CHECK(chord(&b, &t, 3, 0, 0, &seen, &down) == 0 && down == 0);
    // the exact time: POINTER_BACK_HOLD_NS after both are down
    pointerBackReset(&b);
    b.prev = 0;
    t = 5000 * MS;
    int out = 0;
    CHECK(pointerBackStep(&b, 1, 1, t, &out) == 0);
    CHECK(pointerBackStep(&b, 1, 1, t + POINTER_BACK_HOLD_NS - 1, &out) == 0);
    CHECK(pointerBackStep(&b, 1, 1, t + POINTER_BACK_HOLD_NS, &out) == 1 && out == 0);
    CHECK(pointerBackStep(&b, 0, 0, t + POINTER_BACK_HOLD_NS + FRAME, &out) == 0 && out == 0);
    // the grip first, then Menu: the hold counts from Menu's press; the cog does not open
    down = 0;
    t += 2000 * MS;
    CHECK(chord(&b, &t, 30, 0, 1, &seen, &down) == 0);
    CHECK(chord(&b, &t, 46, 1, 1, &seen, &down) == 1 && down == 0);
    CHECK(chord(&b, &t, 2, 0, 0, &seen, &down) == 0 && down == 0);
    // the chord building and given up (the grip let go early): no chord, and no cog either
    down = 0;
    CHECK(chord(&b, &t, 20, 1, 1, &seen, &down) == 0);
    CHECK(chord(&b, &t, 60, 1, 0, &seen, &down) == 0 && down == 0);
    CHECK(chord(&b, &t, 2, 0, 0, &seen, &down) == 0 && down == 0);
    // Menu first, the grip a moment later: the chord, counted from the grip
    CHECK(chord(&b, &t, 10, 1, 0, &seen, &down) == 0);
    CHECK(chord(&b, &t, 44, 1, 1, &seen, &down) == 0);
    CHECK(chord(&b, &t, 2, 1, 1, &seen, &down) == 1 && down == 0);
    CHECK(chord(&b, &t, 2, 0, 0, &seen, &down) == 0 && down == 0);
    // the grip let go and taken again: the hold starts over
    CHECK(chord(&b, &t, 30, 1, 1, &seen, &down) == 0);
    CHECK(chord(&b, &t, 1, 1, 0, &seen, &down) == 0);
    CHECK(chord(&b, &t, 44, 1, 1, &seen, &down) == 0);
    CHECK(chord(&b, &t, 2, 1, 1, &seen, &down) == 1);
    CHECK(chord(&b, &t, 2, 0, 0, &seen, &down) == 0 && down == 0);
    // Menu held alone past the hold time goes through as held (a binding that holds still works),
    // and a grip that comes after that is no chord
    down = 0;
    CHECK(chord(&b, &t, 44, 1, 0, &seen, &down) == 0 && down == 0);
    CHECK(chord(&b, &t, 2, 1, 0, &seen, &down) == 0 && seen == 1);
    CHECK(chord(&b, &t, 90, 1, 1, &seen, &down) == 0 && seen == 1);
    CHECK(chord(&b, &t, 1, 0, 0, &seen, &down) == 0 && seen == 0);
    // the grip alone does nothing
    down = 0;
    CHECK(chord(&b, &t, 90, 0, 1, &seen, &down) == 0 && down == 0);
}

int main(void) {
    testDwellCells();
    testEyesState();
    testEyesCell();
    testDotSize();
    testRowAt();
    testGazeFall();
    testTabSlots();
    testDwellTolerance();
    testGazeSteady();
    testPointerBack();
    return checksDone("xr_access");
}
