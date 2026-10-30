// The binding table at work (xr_bindrun.h): a sample bindings.ini changes the
// host button a trigger press sends and the app command the Y button fires,
// while the defaults send what they always did; presses and releases come
// once however many controls share an action; letting go holds the controls
// still down back; a whole stick's keys; the Guide chord handed to
// padGuideStep's hold; the log's summary. New in this fork.
#include <stdio.h>
#include <string.h>

#include "check.h"
#include "xr_bindrun.h"

#define S(name) XR_BIND_BIT(XR_BIND_SRC_##name)

static XrBindSet g_set;
static XrBindEdges g_e;

static const char* SAMPLE =
    "[pointer]\n"
    "right.trigger = mouse:right\n"
    "right.y = app:ray\n"
    "right.x = key:ctrl+F\n"
    "left.bumper = key:F\n"
    "left.stick = stick:wasd\n"
    "left.stick_up = key:space\n"
    "right.stick = stick:arrows\n"
    "right.a+right.b = wheel:up\n"
    "[gamepad]\n"
    "right.grip = key:R\n"
    "left.view+right.menu = none\n"
    "left.stick = stick:dpad\n"
    "[app \"Time Crisis 2\"]\n"
    "pointer.right.trigger = mouse:left\n"
    "pointer.right.y = app:cog\n";

static int has(const XrBindAction* list, int n, int kind, int code, int mods) {
    XrBindAction a = { kind, code, mods };
    for (int i = 0; i < n; i++) {
        if (xrBindActionEqual(&list[i], &a)) {
            return 1;
        }
    }
    return 0;
}

// The host buttons a frame with these controls down sends, Moonlight's
// BUTTON_ numbers as digits in order
static int hostButtonsFor(const XrBindTable* t, uint64_t down) {
    XrBindRun r;
    xrBindRunReset(&r);
    xrBindRunStep(&r, t, down, &g_e);
    int mask = xrBindEdgesMouse(&g_e);
    int host = 0;
    for (int i = 0; i < XR_BIND_MOUSE_BUTTONS; i++) {
        if (mask & (1 << i)) {
            host = host * 10 + xrBindHostButton(i);
        }
    }
    return host;
}

// The app command a press of these fires, or -1
static int appFor(const XrBindTable* t, uint64_t down) {
    XrBindRun r;
    xrBindRunReset(&r);
    xrBindRunStep(&r, t, down, &g_e);
    for (int i = 0; i < g_e.pressedCount; i++) {
        if (g_e.pressed[i].kind == XR_BIND_ACT_APP) {
            return g_e.pressed[i].code;
        }
    }
    return -1;
}

static void testSampleChangesWhatIsSent(void) {
    CHECK(xrBindParse(&g_set, SAMPLE, NULL) == 0);
    XrBindTable def, t, app;
    xrBindResolve(NULL, XR_BIND_MODE_POINTER, NULL, &def);
    xrBindResolve(&g_set, XR_BIND_MODE_POINTER, NULL, &t);
    xrBindResolve(&g_set, XR_BIND_MODE_POINTER, "time crisis 2", &app);

    // The right trigger: Moonlight's BUTTON_LEFT (1) by default, BUTTON_RIGHT
    // (3) with the sample, and left again for the app that puts it back
    CHECK(hostButtonsFor(&def, S(RIGHT_TRIGGER)) == 1);
    CHECK(hostButtonsFor(&t, S(RIGHT_TRIGGER)) == 3);
    CHECK(hostButtonsFor(&app, S(RIGHT_TRIGGER)) == 1);
    // The left trigger is still a left click
    CHECK(hostButtonsFor(&t, S(LEFT_TRIGGER)) == 1);
    // The bumpers are back and forward by default (BUTTON_X1 4, BUTTON_X2 5)
    CHECK(hostButtonsFor(&def, S(LEFT_BUMPER)) == 4);
    CHECK(hostButtonsFor(&def, S(RIGHT_BUMPER)) == 5);
    CHECK(hostButtonsFor(&def, S(LEFT_BUMPER) | S(RIGHT_BUMPER)) == 45);
    // ...and the sample makes the left one a key
    CHECK(hostButtonsFor(&t, S(LEFT_BUMPER)) == 0);

    // Y: the keyboard by default, the ray with the sample, the cog for the app
    CHECK(appFor(&def, S(RIGHT_Y)) == XR_BIND_APP_KEYBOARD);
    CHECK(appFor(&t, S(RIGHT_Y)) == XR_BIND_APP_RAY);
    CHECK(appFor(&app, S(RIGHT_Y)) == XR_BIND_APP_COG);
    // The other idle controls' defaults
    CHECK(appFor(&def, S(RIGHT_X)) == XR_BIND_APP_3D);
    CHECK(appFor(&def, S(LEFT_DPAD_UP)) == XR_BIND_APP_RECENTRE);
    CHECK(appFor(&def, S(RIGHT_MENU)) == XR_BIND_APP_COG);
    CHECK(appFor(&def, S(LEFT_DPAD_LEFT)) == -1);
    CHECK(appFor(&def, S(LEFT_VIEW)) == -1);

    // A and B together are the sample's chord, a wheel notch, and click
    // nothing of their own
    XrBindRun r;
    xrBindRunReset(&r);
    xrBindRunStep(&r, &t, S(RIGHT_A) | S(RIGHT_B), &g_e);
    CHECK(xrBindEdgesMouse(&g_e) == 0);
    CHECK(g_e.pressedCount == 1 && has(g_e.pressed, 1, XR_BIND_ACT_WHEEL, XR_BIND_WHEEL_UP, 0));
    // A alone is still a right click
    CHECK(hostButtonsFor(&t, S(RIGHT_A)) == 3);
}

// Presses and releases come once a press, whoever holds the action
static void testEdges(void) {
    CHECK(xrBindParse(&g_set, SAMPLE, NULL) == 0);
    XrBindTable t;
    xrBindResolve(&g_set, XR_BIND_MODE_POINTER, NULL, &t);
    xrBindTableExpandSticks(&t);
    XrBindRun r;
    xrBindRunReset(&r);

    xrBindRunStep(&r, &t, S(RIGHT_X), &g_e);
    CHECK(g_e.pressedCount == 1 && has(g_e.pressed, 1, XR_BIND_ACT_KEY, 'F', XR_BIND_MOD_CTRL));
    CHECK(g_e.releasedCount == 0 && g_e.heldCount == 1);
    // Held: nothing new
    xrBindRunStep(&r, &t, S(RIGHT_X), &g_e);
    CHECK(g_e.pressedCount == 0 && g_e.releasedCount == 0 && g_e.heldCount == 1);
    // The left bumper is plain F, another key from ctrl+F
    xrBindRunStep(&r, &t, S(RIGHT_X) | S(LEFT_BUMPER), &g_e);
    CHECK(g_e.pressedCount == 1 && has(g_e.pressed, 1, XR_BIND_ACT_KEY, 'F', 0));
    xrBindRunStep(&r, &t, S(LEFT_BUMPER), &g_e);
    CHECK(g_e.pressedCount == 0 && g_e.releasedCount == 1
          && has(g_e.released, 1, XR_BIND_ACT_KEY, 'F', XR_BIND_MOD_CTRL));
    xrBindRunStep(&r, &t, 0, &g_e);
    CHECK(g_e.releasedCount == 1 && has(g_e.released, 1, XR_BIND_ACT_KEY, 'F', 0));
    CHECK(g_e.heldCount == 0);

    // Two controls on the same action: one press, one release
    xrBindRunStep(&r, &t, S(RIGHT_A), &g_e);
    CHECK(g_e.pressedCount == 1 && xrBindEdgesMouse(&g_e) == 2);
    xrBindRunStep(&r, &t, S(RIGHT_A) | S(LEFT_DPAD_DOWN), &g_e);
    CHECK(g_e.pressedCount == 0 && g_e.heldCount == 1);
    xrBindRunStep(&r, &t, S(LEFT_DPAD_DOWN), &g_e);
    CHECK(g_e.releasedCount == 0 && xrBindEdgesMouse(&g_e) == 2);
    xrBindRunStep(&r, &t, 0, &g_e);
    CHECK(g_e.releasedCount == 1 && xrBindEdgesMouse(&g_e) == 0);

    // Letting go: what is held comes up, and what is still down stays out
    // until it is let go and pressed again
    xrBindRunStep(&r, &t, S(RIGHT_X) | S(RIGHT_TRIGGER), &g_e);
    CHECK(g_e.heldCount == 2);
    xrBindRunLetGo(&r, S(RIGHT_X) | S(RIGHT_TRIGGER), &g_e);
    CHECK(g_e.releasedCount == 2 && g_e.heldCount == 0 && g_e.pressedCount == 0);
    xrBindRunStep(&r, &t, S(RIGHT_X) | S(RIGHT_TRIGGER), &g_e);
    CHECK(g_e.heldCount == 0 && g_e.pressedCount == 0);
    xrBindRunStep(&r, &t, S(RIGHT_TRIGGER), &g_e);
    CHECK(g_e.heldCount == 0);
    xrBindRunStep(&r, &t, S(RIGHT_TRIGGER) | S(RIGHT_X), &g_e);
    CHECK(g_e.pressedCount == 1 && has(g_e.pressed, 1, XR_BIND_ACT_KEY, 'F', XR_BIND_MOD_CTRL));
    CHECK(xrBindEdgesMouse(&g_e) == 0);
    xrBindRunStep(&r, &t, 0, &g_e);
    xrBindRunStep(&r, &t, S(RIGHT_TRIGGER), &g_e);
    CHECK(xrBindEdgesMouse(&g_e) == 2);
}

// A whole stick's keys, where its directions have none of their own
static void testSticks(void) {
    CHECK(xrBindParse(&g_set, SAMPLE, NULL) == 0);
    XrBindTable t;
    xrBindResolve(&g_set, XR_BIND_MODE_POINTER, NULL, &t);
    xrBindTableExpandSticks(&t);
    CHECK(t.single[XR_BIND_SRC_LEFT_STICK_UP].kind == XR_BIND_ACT_KEY
          && t.single[XR_BIND_SRC_LEFT_STICK_UP].code == 0x20);
    CHECK(t.single[XR_BIND_SRC_LEFT_STICK_DOWN].code == 'S');
    CHECK(t.single[XR_BIND_SRC_LEFT_STICK_LEFT].code == 'A');
    CHECK(t.single[XR_BIND_SRC_LEFT_STICK_RIGHT].code == 'D');
    CHECK(t.single[XR_BIND_SRC_RIGHT_STICK_UP].code == 0x26);
    CHECK(t.single[XR_BIND_SRC_RIGHT_STICK_DOWN].code == 0x28);
    CHECK(t.single[XR_BIND_SRC_RIGHT_STICK_LEFT].code == 0x25);
    CHECK(t.single[XR_BIND_SRC_RIGHT_STICK_RIGHT].code == 0x27);
    // The stick pushed up past half way
    PadHand l, rh;
    memset(&l, 0, sizeof(l));
    memset(&rh, 0, sizeof(rh));
    l.stickY = 0.8f;
    uint64_t down = xrBindSourcesDown(&l, &rh, 0);
    CHECK(down == S(LEFT_STICK_UP));
    XrBindRun r;
    xrBindRunReset(&r);
    xrBindRunStep(&r, &t, down, &g_e);
    CHECK(g_e.pressedCount == 1 && has(g_e.pressed, 1, XR_BIND_ACT_KEY, 0x20, 0));

    // A scrolling stick has no keys
    XrBindTable def;
    xrBindResolve(NULL, XR_BIND_MODE_POINTER, NULL, &def);
    xrBindTableExpandSticks(&def);
    CHECK(def.single[XR_BIND_SRC_LEFT_STICK_UP].kind == XR_BIND_ACT_NONE);
    // In gamepad mode dpad is the pad's d-pad
    XrBindTable g;
    xrBindResolve(&g_set, XR_BIND_MODE_GAMEPAD, NULL, &g);
    xrBindTableExpandSticks(&g);
    CHECK(g.single[XR_BIND_SRC_LEFT_STICK_UP].kind == XR_BIND_ACT_PAD
          && g.single[XR_BIND_SRC_LEFT_STICK_UP].code == PAD_UP);
    CHECK(g.single[XR_BIND_SRC_LEFT_STICK_RIGHT].code == PAD_RIGHT);
    CHECK(xrBindPadButtons(&g, S(LEFT_STICK_LEFT)) == PAD_LEFT);
}

static void testGuideAndSame(void) {
    XrBindTable def, def2;
    xrBindResolve(NULL, XR_BIND_MODE_GAMEPAD, NULL, &def);
    xrBindResolve(NULL, XR_BIND_MODE_GAMEPAD, NULL, &def2);
    CHECK(xrBindTableSame(&def, &def2));
    // The default Guide chord goes to padGuideStep's hold: View and Menu come
    // through as Back and Start for it
    CHECK(xrBindTableGuideHold(&def));
    CHECK(def.chordCount == 0);
    CHECK(xrBindPadButtons(&def, S(LEFT_VIEW) | S(RIGHT_MENU)) == (PAD_BACK | PAD_START));
    CHECK(!xrBindTableSame(&def, &def2));
    // Asked again, there is nothing more to take
    CHECK(!xrBindTableGuideHold(&def));

    // The sample takes the chord away: no hold, Back and Start as they are
    CHECK(xrBindParse(&g_set, SAMPLE, NULL) == 0);
    XrBindTable t;
    xrBindResolve(&g_set, XR_BIND_MODE_GAMEPAD, NULL, &t);
    CHECK(!xrBindTableSame(&t, &def2));
    CHECK(!xrBindTableGuideHold(&t));
    CHECK(xrBindPadButtons(&t, S(LEFT_VIEW) | S(RIGHT_MENU)) == (PAD_BACK | PAD_START));

    // A Guide chord with Menu bound to something else fires at once
    const char* moved = "[gamepad]\nright.menu = pad:a\n";
    CHECK(xrBindParse(&g_set, moved, NULL) == 0);
    xrBindResolve(&g_set, XR_BIND_MODE_GAMEPAD, NULL, &t);
    CHECK(!xrBindTableGuideHold(&t));
    CHECK(xrBindPadButtons(&t, S(LEFT_VIEW) | S(RIGHT_MENU)) == PAD_GUIDE);
    // The defaults written out are the defaults still
    const char* same = "[gamepad]\nleft.trigger = pad:lt\nleft.view+right.menu = pad:guide\n";
    CHECK(xrBindParse(&g_set, same, NULL) == 0);
    xrBindResolve(&g_set, XR_BIND_MODE_GAMEPAD, NULL, &t);
    CHECK(xrBindTableSame(&t, &def2));
    CHECK(xrBindTableGuideHold(&t));

    // A button on pad:lt pulls that trigger all the way
    const char* pull = "[gamepad]\nright.x = pad:lt\n";
    CHECK(xrBindParse(&g_set, pull, NULL) == 0);
    xrBindResolve(&g_set, XR_BIND_MODE_GAMEPAD, NULL, &t);
    XrBindRun r;
    xrBindRunReset(&r);
    xrBindRunStep(&r, &t, S(RIGHT_X) | S(RIGHT_A), &g_e);
    int pulled = 0;
    CHECK(xrBindEdgesPad(&g_e, &pulled) == PAD_A);
    CHECK(pulled == PAD_HELD_LT);
}

static void testSummary(void) {
    char line[512];
    XrBindTable def;
    xrBindResolve(NULL, XR_BIND_MODE_POINTER, NULL, &def);
    CHECK(xrBindSummary(&def, line, sizeof(line)) == 0 && line[0] == '\0');
    CHECK(xrBindParse(&g_set, SAMPLE, NULL) == 0);
    XrBindTable t;
    xrBindResolve(&g_set, XR_BIND_MODE_GAMEPAD, NULL, &t);
    int n = xrBindSummary(&t, line, sizeof(line));
    CHECK(n == 3);
    CHECK(strstr(line, "right.grip = key:R") != NULL);
    CHECK(strstr(line, "left.stick = stick:dpad") != NULL);
    CHECK(strstr(line, "left.view+right.menu = none") != NULL);
    xrBindResolve(&g_set, XR_BIND_MODE_POINTER, NULL, &t);
    n = xrBindSummary(&t, line, sizeof(line));
    CHECK(n == 8);
    CHECK(strstr(line, "right.trigger = mouse:right") != NULL);
    CHECK(strstr(line, "right.a+right.b = wheel:up") != NULL);
    // A short buffer is cut, never overrun
    char tiny[8];
    memset(tiny, 'x', sizeof(tiny));
    xrBindSummary(&t, tiny, sizeof(tiny));
    CHECK(strlen(tiny) == 7);
}

int main(void) {
    testSampleChangesWhatIsSent();
    testEdges();
    testSticks();
    testGuideAndSame();
    testSummary();
    return checksDone("xr_bindrun");
}
