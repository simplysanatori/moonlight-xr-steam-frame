// Native (lane a11y-2): the headset button's gestures and what they do to the mouse buttons,
// table driven: a script of presses and releases (ms) and the actions it must come to, in order
#include <stdio.h>
#include <string.h>

#include "check.h"
#include "xr_assistclick.h"

#define MS 1000000LL

typedef struct {
    const char* name;
    int steps[16][2];    // { ms, 1 down / 0 up }, ends at { -1, 0 }
    int endMs;           // ticks run to here (every 11 ms)
    int want[8];         // the actions, in order, ends at ASSIST_NONE
} Script;

static const Script SCRIPTS[] = {
    { "one press", { { 100, 1 }, { 180, 0 }, { -1, 0 } }, 1000, { ASSIST_LEFT } },
    { "two presses", { { 100, 1 }, { 180, 0 }, { 300, 1 }, { 380, 0 }, { -1, 0 } }, 1500, { ASSIST_DOUBLE } },
    { "three presses", { { 100, 1 }, { 160, 0 }, { 260, 1 }, { 320, 0 }, { 420, 1 }, { 480, 0 }, { -1, 0 } }, 1500,
      { ASSIST_RIGHT } },
    { "two presses too far apart", { { 100, 1 }, { 180, 0 }, { 700, 1 }, { 780, 0 }, { -1, 0 } }, 2000,
      { ASSIST_LEFT, ASSIST_LEFT } },
    { "a hold is a drag", { { 100, 1 }, { 1500, 0 }, { -1, 0 } }, 2500, { ASSIST_DRAG_DOWN, ASSIST_DRAG_UP } },
    { "a press just short of the hold", { { 100, 1 }, { 480, 0 }, { -1, 0 } }, 1500, { ASSIST_LEFT } },
    { "tap then hold is a drag", { { 100, 1 }, { 160, 0 }, { 250, 1 }, { 1200, 0 }, { -1, 0 } }, 2000,
      { ASSIST_DRAG_DOWN, ASSIST_DRAG_UP } },
    { "3 s hold escapes, the release does nothing", { { 100, 1 }, { 3600, 0 }, { -1, 0 } }, 4500,
      { ASSIST_DRAG_DOWN, ASSIST_ESCAPE } },
    { "after the escape the next press clicks", { { 100, 1 }, { 3300, 0 }, { 4000, 1 }, { 4050, 0 }, { -1, 0 } }, 5000,
      { ASSIST_DRAG_DOWN, ASSIST_ESCAPE, ASSIST_LEFT } },
    { "a repeated down is one press", { { 100, 1 }, { 120, 1 }, { 150, 0 }, { -1, 0 } }, 1000, { ASSIST_LEFT } },
    { "an up with no down is nothing", { { 100, 0 }, { -1, 0 } }, 1000, { ASSIST_NONE } },
};

static void testScripts(void) {
    for (size_t s = 0; s < sizeof SCRIPTS / sizeof SCRIPTS[0]; s++) {
        const Script* sc = &SCRIPTS[s];
        AuxClick g;
        auxClickReset(&g);
        int got[16], n = 0, step = 0;
        for (int ms = 0; ms <= sc->endMs; ms++) {
            if (ms % 11 == 0) {
                int a = auxClickTick(&g, ms * MS);
                if (a != ASSIST_NONE && n < 16) got[n++] = a;
            }
            while (sc->steps[step][0] == ms) {
                int a = auxClickEdge(&g, sc->steps[step][1], ms * MS);
                if (a != ASSIST_NONE && n < 16) got[n++] = a;
                step++;
            }
        }
        int ok = 1, w = 0;
        for (; w < 8 && sc->want[w] != ASSIST_NONE; w++) {
            if (w >= n || got[w] != sc->want[w]) ok = 0;
        }
        if (n != w) ok = 0;
        CHECK(ok);
        if (!ok) {
            fprintf(stderr, "  %s: got", sc->name);
            for (int i = 0; i < n; i++) fprintf(stderr, " %s", assistActionName(got[i]));
            fprintf(stderr, "\n");
        }
    }
}

// The buttons down over time for each action
static void testPlay(void) {
    AssistPlay p;
    assistPlayReset(&p);
    int press = 0;
    CHECK(assistPlayButtons(&p, 0, &press) == 0 && !press && !assistPlayBusy(&p, 0));
    // a click: the left button for 60 ms, one press edge
    assistPlayStart(&p, ASSIST_LEFT, 1000 * MS);
    CHECK(assistPlayButtons(&p, 1000 * MS, &press) == ASSIST_BTN_LEFT && press);
    CHECK(assistPlayButtons(&p, 1030 * MS, &press) == ASSIST_BTN_LEFT && !press);
    CHECK(assistPlayButtons(&p, 1061 * MS, &press) == 0 && !assistPlayBusy(&p, 1061 * MS));
    // a double click: two presses, two edges
    assistPlayStart(&p, ASSIST_DOUBLE, 2000 * MS);
    int edges = 0, downFrames = 0;
    for (int ms = 0; ms < 300; ms += 5) {
        int b = assistPlayButtons(&p, (2000 + ms) * MS, &press);
        edges += press;
        downFrames += b == ASSIST_BTN_LEFT;
    }
    CHECK(edges == 2 && downFrames == 24);
    // a right click
    assistPlayStart(&p, ASSIST_RIGHT, 3000 * MS);
    CHECK(assistPlayButtons(&p, 3010 * MS, &press) == ASSIST_BTN_RIGHT && press);
    CHECK(assistPlayButtons(&p, 3100 * MS, NULL) == 0);
    // a drag: held until let go, the escape lets it go too
    assistPlayStart(&p, ASSIST_DRAG_DOWN, 4000 * MS);
    CHECK(assistPlayButtons(&p, 4000 * MS, &press) == ASSIST_BTN_LEFT && press);
    CHECK(assistPlayButtons(&p, 9000 * MS, &press) == ASSIST_BTN_LEFT && !press && assistPlayBusy(&p, 9000 * MS));
    assistPlayStart(&p, ASSIST_DRAG_UP, 9100 * MS);
    CHECK(assistPlayButtons(&p, 9100 * MS, NULL) == 0);
    assistPlayStart(&p, ASSIST_DRAG_DOWN, 10000 * MS);
    assistPlayStart(&p, ASSIST_ESCAPE, 11000 * MS);
    CHECK(assistPlayButtons(&p, 11000 * MS, NULL) == 0 && !assistPlayBusy(&p, 11000 * MS));
}

// Native (lane radial-dwell): a press spent on something else (letting the radial menu's drag go)
// clicks nothing on its release and drags nothing when held; a 3 s hold still escapes
static void testSpend(void) {
    AuxClick g;
    auxClickReset(&g);
    int any = 0;
    auxClickEdge(&g, 1, 100 * MS);
    auxClickSpend(&g);
    for (int ms = 100; ms <= 1500; ms += 11) any |= auxClickTick(&g, ms * MS) != ASSIST_NONE;
    any |= auxClickEdge(&g, 0, 1500 * MS) != ASSIST_NONE;
    for (int ms = 1500; ms <= 2500; ms += 11) any |= auxClickTick(&g, ms * MS) != ASSIST_NONE;
    CHECK(!any);
    // the next press is a click again
    auxClickEdge(&g, 1, 3000 * MS);
    auxClickEdge(&g, 0, 3080 * MS);
    int a = ASSIST_NONE;
    for (int ms = 3080; ms <= 3600 && a == ASSIST_NONE; ms += 11) a = auxClickTick(&g, ms * MS);
    CHECK(a == ASSIST_LEFT);
    // spent and held 3 s: the escape still comes
    auxClickEdge(&g, 1, 5000 * MS);
    auxClickSpend(&g);
    a = ASSIST_NONE;
    for (int ms = 5000; ms <= 8200 && a == ASSIST_NONE; ms += 11) a = auxClickTick(&g, ms * MS);
    CHECK(a == ASSIST_ESCAPE);
    // nothing down: nothing to spend
    auxClickReset(&g);
    auxClickSpend(&g);
    CHECK(!g.spent);
}

int main(void) {
    testScripts();
    testPlay();
    testSpend();
    return checksDone("xr_assistclick");
}
