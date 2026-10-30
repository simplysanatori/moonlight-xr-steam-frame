// Native (lane a11y-tab): the Accessibility tab's pure parts: the dwell row's cells, the eye
// tracking line, the pointer row's cells, the eyes pointer's fallback to the head, and the tab
// bar's uneven slots
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
    CHECK(accessEyesState(0, 1, 1, 0) == ACCESS_EYES_UNAVAILABLE);
    CHECK(accessEyesState(0, 0, 0, 0) == ACCESS_EYES_UNAVAILABLE);
    // a gaze that arrived is ready, even with the system's flag at 0
    CHECK(accessEyesState(1, 1, 0, 1) == ACCESS_EYES_READY);
    // offered but not asked for
    CHECK(accessEyesState(1, 0, 0, 0) == ACCESS_EYES_NOT_ASKED);
    // asked, but the system says no (or the binding was refused)
    CHECK(accessEyesState(1, 1, 0, 0) == ACCESS_EYES_UNAVAILABLE);
    CHECK(accessEyesState(1, 1, 1, 0) == ACCESS_EYES_WAITING);
    CHECK(accessEyesState(1, 1, 1, 1) == ACCESS_EYES_READY);
}

static void testPointerCells(void) {
    // Eyes only where it works, or where it is already the setting
    CHECK(accessPointerCells(ACCESS_EYES_READY, POINTER_SOURCE_CONTROLLER) == 3);
    for (int st = ACCESS_EYES_WAITING; st < ACCESS_EYES_COUNT; st++) {
        CHECK(accessPointerCells(st, POINTER_SOURCE_CONTROLLER) == 2);
        CHECK(accessPointerCells(st, POINTER_SOURCE_HEAD) == 2);
        CHECK(accessPointerCells(st, POINTER_SOURCE_GAZE) == 3);
    }
    CHECK(COG_ACCESS_POINTER_CELLS == 3);
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
        CHECK(cogTrackSteps(COG_TAB_ACCESS, row) == 0);
    }
    CHECK(cogRowV(COG_TAB_ACCESS, COG_ACCESS_ROW_COUNT - 1) + cogRowHalf(COG_TAB_ACCESS) < COG_ACCESS_STATUS_V);
    CHECK(COG_ACCESS_STATUS_V < COG_ACCESS_HELP_T && COG_ACCESS_HELP_T < 1.0f);
}

int main(void) {
    testDwellCells();
    testEyesState();
    testPointerCells();
    testGazeFall();
    testTabSlots();
    return checksDone("xr_access");
}
