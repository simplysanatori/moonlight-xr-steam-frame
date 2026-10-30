// Mouse emulation on a physical gamepad (PLAN 2b.4): the curve, the chord and
// its clock, the neutral state the game is given, the buttons, keys, wheel and
// pointer while it is a mouse, and letting go of everything.
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "xr_padmouse.h"

#define MS 1000000LL
// A stick at full tilt in the packet's units
#define STICK 32767

// What the sink heard, in order
enum { EV_MOVE = 1, EV_BUTTON, EV_SCROLL, EV_HSCROLL, EV_KEY, EV_KEYBOARD, EV_MODE };
typedef struct {
    int kind;
    int a;
    int b;
} Ev;
static Ev ev[512];
static int evCount;
static long sumDx, sumDy, sumScroll, sumHScroll;

static void rec(int kind, int a, int b) {
    if (evCount < 512) {
        ev[evCount].kind = kind;
        ev[evCount].a = a;
        ev[evCount].b = b;
        evCount++;
    }
}

static void sMove(void* u, int dx, int dy) {
    (void)u;
    sumDx += dx;
    sumDy += dy;
    rec(EV_MOVE, dx, dy);
}
static void sButton(void* u, int b, int down) { (void)u; rec(EV_BUTTON, b, down); }
static void sScroll(void* u, int a) { (void)u; sumScroll += a; rec(EV_SCROLL, a, 0); }
static void sHScroll(void* u, int a) { (void)u; sumHScroll += a; rec(EV_HSCROLL, a, 0); }
static void sKey(void* u, int vk, int down) { (void)u; rec(EV_KEY, vk, down); }
static void sKeyboard(void* u) { (void)u; rec(EV_KEYBOARD, 0, 0); }
static void sMode(void* u, int on) { (void)u; rec(EV_MODE, on, 0); }

static const PadMouseSink SINK = { sMove, sButton, sScroll, sHScroll, sKey, sKeyboard, sMode, NULL };

static void clearEvents(void) {
    evCount = 0;
    sumDx = sumDy = sumScroll = sumHScroll = 0;
}

static int count(int kind) {
    int n = 0;
    for (int i = 0; i < evCount; i++) n += ev[i].kind == kind;
    return n;
}

// Whether the sink heard this exact button or key event
static int heard(int kind, int a, int b) {
    for (int i = 0; i < evCount; i++)
        if (ev[i].kind == kind && ev[i].a == a && ev[i].b == b) return 1;
    return 0;
}

static PadMouseIn rest(void) {
    PadMouseIn in;
    memset(&in, 0, sizeof(in));
    return in;
}

static PadMouseIn held(int buttons) {
    PadMouseIn in = rest();
    in.buttons = buttons;
    return in;
}

// Steps for ms milliseconds in 10 ms steps from *now, returning the last result
static PadMouseOut run(PadMouse* pm, const PadMouseIn* in, int ms, int64_t* now) {
    PadMouseOut out = { 0, 0 };
    for (int t = 0; t < ms; t += 10) {
        *now += 10 * MS;
        out = padMouseStep(pm, in, 1, *now, &SINK);
    }
    return out;
}

// Turns the mode on with Start held for the second (from time *now), then lets go
static void turnOn(PadMouse* pm, int64_t* now) {
    PadMouseIn start = held(PAD_START);
    PadMouseIn none = rest();
    padMouseStep(pm, &start, 1, *now, &SINK);
    run(pm, &start, 1000, now);
    run(pm, &none, 20, now);
}

static void testCurve(void) {
    CHECK(padMouseTravel(0.0f) == 0.0f);
    CHECK(padMouseTravel(PADMOUSE_DEADZONE) == 0.0f);
    CHECK(padMouseTravel(PADMOUSE_DEADZONE - 0.01f) == 0.0f);
    CHECK_NEAR(padMouseTravel(1.0f), 1.0f, 1e-6);
    CHECK_NEAR(padMouseTravel(1.5f), 1.0f, 1e-6);   // held to full
    CHECK_NEAR(padMouseTravel((1.0f + PADMOUSE_DEADZONE) / 2.0f), 0.5f, 1e-5);
    CHECK(padMouseSpeed(0.10f) == 0.0f);
    CHECK_NEAR(padMouseSpeed(1.0f), PADMOUSE_SPEED_LINEAR + PADMOUSE_SPEED_SQUARE, 1e-3);
    CHECK_NEAR(padMouseSpeed(1.0f), 1800.0f, 1e-3);
    // Half way along the travel: the linear part and a quarter of the square
    CHECK_NEAR(padMouseSpeed((1.0f + PADMOUSE_DEADZONE) / 2.0f), 30.0f + 1740.0f / 4.0f, 1e-2);
    // Slow near the dead zone, fast at the end, never going down
    float prev = -1.0f;
    int monotone = 1;
    for (int i = 0; i <= 100; i++) {
        float s = padMouseSpeed((float)i / 100.0f);
        if (s < prev) monotone = 0;
        prev = s;
    }
    CHECK(monotone);
    CHECK(padMouseSpeed(0.30f) < 100.0f);
    CHECK(padMouseSpeed(0.60f) > 250.0f && padMouseSpeed(0.60f) < 700.0f);
    CHECK_NEAR(padMouseScrollSpeed(1.0f), PADMOUSE_SCROLL_FULL, 1e-3);
    CHECK(padMouseScrollSpeed(0.10f) == 0.0f);
    // Settings are held to their lists
    CHECK(padMouseChordClamp(-1) == PAD_MOUSE_CHORD_START_HOLD && padMouseChordClamp(3) == PAD_MOUSE_CHORD_START_HOLD);
    CHECK(padMouseChordClamp(PAD_MOUSE_CHORD_OFF) == PAD_MOUSE_CHORD_OFF);
    CHECK(padMouseScrollClamp(9) == PAD_MOUSE_SCROLL_RIGHT && padMouseScrollClamp(PAD_MOUSE_SCROLL_NONE) == PAD_MOUSE_SCROLL_NONE);
    CHECK(padArbitrationClamp(-5) == PAD_ARB_POINTER && padArbitrationClamp(PAD_ARB_SLEEP) == PAD_ARB_SLEEP);
    CHECK(padArbitrationClamp(3) == PAD_ARB_POINTER);
}

static void testChordTiming(void) {
    PadMouse pm;
    padMouseInit(&pm);
    clearEvents();
    int64_t now = 5000 * MS;
    PadMouseIn start = held(PAD_START);
    PadMouseOut out = padMouseStep(&pm, &start, 1, now, &SINK);
    CHECK(!out.neutral && out.strip == 0 && !pm.on);   // the game sees Start meanwhile
    CHECK(padMouseWantsTick(&pm));                     // the clock runs
    out = run(&pm, &start, 980, &now);                 // 980 ms in
    CHECK(!pm.on && !out.neutral && count(EV_MODE) == 0);
    out = run(&pm, &start, 20, &now);                  // the second
    CHECK(pm.on && out.neutral && count(EV_MODE) == 1 && heard(EV_MODE, 1, 0));
    CHECK(padMouseWantsTick(&pm));
    // Start is still held: it stays out of the mouse, and nothing toggles again however long
    out = run(&pm, &start, 3000, &now);
    CHECK(pm.on && out.neutral && count(EV_MODE) == 1);
    PadMouseIn none = rest();
    out = run(&pm, &none, 30, &now);
    CHECK(pm.on && out.neutral && pm.strip == 0);

    // Held again for a second: back to the game
    clearEvents();
    out = run(&pm, &start, 990, &now);
    CHECK(pm.on && out.neutral);
    out = run(&pm, &start, 20, &now);
    CHECK(!pm.on && !out.neutral && count(EV_MODE) == 1 && heard(EV_MODE, 0, 0));
    // The game does not see the Start that ended it, while it is held
    CHECK((out.strip & PAD_START) != 0);
    out = run(&pm, &start, 2000, &now);
    CHECK(!pm.on && (out.strip & PAD_START) != 0 && count(EV_MODE) == 1);
    out = run(&pm, &none, 20, &now);
    CHECK(out.strip == 0 && !out.neutral);
    CHECK(!padMouseWantsTick(&pm));

    // Let go early: the clock starts again, so two short holds are no hold
    clearEvents();
    run(&pm, &start, 900, &now);
    run(&pm, &none, 20, &now);
    run(&pm, &start, 900, &now);
    CHECK(!pm.on && count(EV_MODE) == 0);
    CHECK(padMouseWantsTick(&pm));
    run(&pm, &none, 20, &now);
    CHECK(!padMouseWantsTick(&pm));

    // Another button in with the Start spoils the hold (this is no longer Start alone)
    PadMouseIn startA = held(PAD_START | PAD_A);
    run(&pm, &start, 500, &now);
    run(&pm, &startA, 20, &now);
    run(&pm, &start, 600, &now);
    CHECK(!pm.on);
    run(&pm, &start, 500, &now);
    CHECK(pm.on);   // 1100 ms since A went
    // ... and sticks do not
    padMouseRelease(&pm, &SINK);
    PadMouseIn startStick = held(PAD_START);
    startStick.leftX = STICK;
    run(&pm, &startStick, 1100, &now);
    CHECK(pm.on);
}

static void testChordChoices(void) {
    PadMouse pm;
    padMouseInit(&pm);
    int64_t now = 0;
    PadMouseIn start = held(PAD_START);
    PadMouseIn startBack = held(PAD_START | PAD_BACK);
    PadMouseIn back = held(PAD_BACK);
    PadMouseIn startBackLb = held(PAD_START | PAD_BACK | PAD_LB);
    PadMouseIn none = rest();

    // Start and Back
    padMouseConfigure(&pm, PAD_MOUSE_CHORD_START_BACK, PAD_MOUSE_SCROLL_RIGHT);
    run(&pm, &start, 2000, &now);
    CHECK(!pm.on);
    run(&pm, &back, 2000, &now);
    CHECK(!pm.on);
    run(&pm, &none, 20, &now);
    run(&pm, &startBackLb, 2000, &now);   // not the pair alone (the disconnect chord is Start, Back, LB, RB)
    CHECK(!pm.on);
    run(&pm, &none, 20, &now);
    PadMouseOut out = run(&pm, &startBack, 1010, &now);
    CHECK(pm.on && out.neutral);
    out = run(&pm, &startBack, 1000, &now);
    CHECK(pm.on);
    run(&pm, &none, 20, &now);
    out = run(&pm, &startBack, 1010, &now);
    CHECK(!pm.on && (out.strip & (PAD_START | PAD_BACK)) == (PAD_START | PAD_BACK));
    run(&pm, &none, 20, &now);

    // Off: nothing begins, and switching it off while it is on ends the mode
    padMouseConfigure(&pm, PAD_MOUSE_CHORD_OFF, PAD_MOUSE_SCROLL_RIGHT);
    run(&pm, &start, 3000, &now);
    run(&pm, &startBack, 3000, &now);
    CHECK(!pm.on);
    CHECK(!padMouseWantsTick(&pm));
    padMouseConfigure(&pm, PAD_MOUSE_CHORD_START_HOLD, PAD_MOUSE_SCROLL_RIGHT);
    run(&pm, &none, 20, &now);
    turnOn(&pm, &now);
    CHECK(pm.on);
    clearEvents();
    padMouseConfigure(&pm, PAD_MOUSE_CHORD_OFF, PAD_MOUSE_SCROLL_RIGHT);
    PadMouseIn a = held(PAD_A);
    out = run(&pm, &a, 20, &now);
    CHECK(!pm.on && !out.neutral);
    CHECK(count(EV_MODE) == 1 && heard(EV_MODE, 0, 0));
    // A bad setting is the default
    padMouseConfigure(&pm, 77, -4);
    CHECK(pm.chord == PAD_MOUSE_CHORD_START_HOLD && pm.scroll == PAD_MOUSE_SCROLL_RIGHT);
}

static void testButtonsAndKeys(void) {
    PadMouse pm;
    padMouseInit(&pm);
    int64_t now = 100 * MS;
    turnOn(&pm, &now);
    clearEvents();
    PadMouseIn in = held(PAD_A);
    PadMouseOut out = run(&pm, &in, 30, &now);
    CHECK(out.neutral && out.strip == 0);
    CHECK(count(EV_BUTTON) == 1 && heard(EV_BUTTON, PADMOUSE_BTN_LEFT, 1));   // once, not each step
    in = rest();
    run(&pm, &in, 20, &now);
    CHECK(count(EV_BUTTON) == 2 && heard(EV_BUTTON, PADMOUSE_BTN_LEFT, 0));

    const struct { int pad; int button; } buttons[] = {
        { PAD_B, PADMOUSE_BTN_RIGHT }, { PAD_X, PADMOUSE_BTN_MIDDLE }, { PAD_LB, PADMOUSE_BTN_X1 }, { PAD_RB, PADMOUSE_BTN_X2 },
    };
    for (int i = 0; i < 4; i++) {
        clearEvents();
        in = held(buttons[i].pad);
        run(&pm, &in, 20, &now);
        CHECK(heard(EV_BUTTON, buttons[i].button, 1) && count(EV_BUTTON) == 1);
        in = rest();
        run(&pm, &in, 20, &now);
        CHECK(heard(EV_BUTTON, buttons[i].button, 0) && count(EV_BUTTON) == 2);
    }
    // Several at once, each its own
    clearEvents();
    in = held(PAD_A | PAD_B | PAD_X);
    run(&pm, &in, 20, &now);
    CHECK(count(EV_BUTTON) == 3);
    in = held(PAD_B);
    run(&pm, &in, 20, &now);
    CHECK(heard(EV_BUTTON, PADMOUSE_BTN_LEFT, 0) && heard(EV_BUTTON, PADMOUSE_BTN_MIDDLE, 0) && !heard(EV_BUTTON, PADMOUSE_BTN_RIGHT, 0));
    in = rest();
    run(&pm, &in, 20, &now);

    // The d-pad is the arrows
    const struct { int pad; int vk; } keys[] = {
        { PAD_UP, 0x26 }, { PAD_DOWN, 0x28 }, { PAD_LEFT, 0x25 }, { PAD_RIGHT, 0x27 },
    };
    CHECK(PADMOUSE_VK_UP == 0x26 && PADMOUSE_VK_DOWN == 0x28 && PADMOUSE_VK_LEFT == 0x25 && PADMOUSE_VK_RIGHT == 0x27);
    for (int i = 0; i < 4; i++) {
        clearEvents();
        in = held(keys[i].pad);
        run(&pm, &in, 50, &now);
        CHECK(heard(EV_KEY, keys[i].vk, 1) && count(EV_KEY) == 1);   // a held key is one down, the host repeats it
        in = rest();
        run(&pm, &in, 20, &now);
        CHECK(heard(EV_KEY, keys[i].vk, 0) && count(EV_KEY) == 2);
    }

    // Y asks for the keyboard once a press, however long it is held
    clearEvents();
    in = held(PAD_Y);
    run(&pm, &in, 200, &now);
    CHECK(count(EV_KEYBOARD) == 1);
    in = rest();
    run(&pm, &in, 20, &now);
    in = held(PAD_Y);
    run(&pm, &in, 20, &now);
    CHECK(count(EV_KEYBOARD) == 2);
    in = rest();
    run(&pm, &in, 20, &now);

    // Buttons with no job do nothing, and the stick clicks and Start are not clicks
    clearEvents();
    in = held(PAD_LS_CLICK | PAD_RS_CLICK | PAD_START | PAD_BACK);
    run(&pm, &in, 50, &now);
    CHECK(count(EV_BUTTON) == 0 && count(EV_KEY) == 0 && count(EV_KEYBOARD) == 0);
    // Buttons beyond the pad's own bits (paddles, the touchpad click) are not read
    in = held(0x100000 | 0x20000);
    run(&pm, &in, 50, &now);
    CHECK(count(EV_BUTTON) == 0);
}

static void testHeldAtToggle(void) {
    // What is held when the mode begins (the chord) is not a click
    PadMouse pm;
    padMouseInit(&pm);
    int64_t now = 0;
    PadMouseIn start = held(PAD_START);
    run(&pm, &start, 1010, &now);
    CHECK(pm.on);
    clearEvents();
    PadMouseIn startA = held(PAD_START | PAD_A);
    run(&pm, &startA, 50, &now);
    CHECK(count(EV_BUTTON) == 1);   // A is pressed after the toggle, with Start still down: a click
    PadMouseIn none = rest();
    run(&pm, &none, 20, &now);
    clearEvents();
    // The buttons held at the moment the chord completes (Start) belong to no one until let go
    padMouseRelease(&pm, &SINK);
    CHECK(!pm.on);
    run(&pm, &start, 1010, &now);
    CHECK(pm.on && (pm.strip & PAD_START) != 0);
    CHECK(count(EV_BUTTON) == 0 && count(EV_KEY) == 0);
}

static void testPointer(void) {
    PadMouse pm;
    padMouseInit(&pm);
    int64_t now = 0;
    turnOn(&pm, &now);

    // Full right for a tenth of a second: 180 pixels, straight
    clearEvents();
    PadMouseIn in = rest();
    in.leftX = STICK;
    run(&pm, &in, 100, &now);
    CHECK(sumDx >= 178 && sumDx <= 182);
    CHECK(sumDy == 0);

    // Up on the stick (y positive) is up on the screen (negative)
    clearEvents();
    in = rest();
    in.leftY = STICK;
    run(&pm, &in, 100, &now);
    CHECK(sumDy <= -178 && sumDy >= -182 && sumDx == 0);

    // Left and down
    clearEvents();
    in = rest();
    in.leftX = -STICK;
    in.leftY = -STICK;
    run(&pm, &in, 100, &now);
    CHECK(sumDx < 0 && sumDy > 0);
    // At 45 degrees full tilt (both axes at the corner) is still full speed along the diagonal
    CHECK(labs(sumDx) >= 125 && labs(sumDx) <= 130 && labs(sumDy) >= 125 && labs(sumDy) <= 130);

    // Inside the dead zone nothing moves
    clearEvents();
    in = rest();
    in.leftX = (int)(0.14f * 32767.0f);
    run(&pm, &in, 500, &now);
    CHECK(sumDx == 0 && count(EV_MOVE) == 0);

    // A gentle push (20 percent: about 9.5 px a second) moves a little, with the part
    // pixels carried between steps instead of lost
    clearEvents();
    in = rest();
    in.leftX = (int)(0.20f * 32767.0f);
    run(&pm, &in, 1000, &now);
    long gentle = sumDx;
    CHECK(gentle >= 8 && gentle <= 11);
    CHECK(count(EV_MOVE) < 15);   // a pixel now and then, not a move on every step
    // Twice the push (40 percent: about 168 px a second) is much more than twice as fast
    clearEvents();
    in.leftX = (int)(0.40f * 32767.0f);
    run(&pm, &in, 1000, &now);
    CHECK(sumDx > 150 && sumDx < 185);
    CHECK(sumDx > 2 * gentle);

    // Stopping stops, and a remainder does not leak into the next push
    clearEvents();
    in = rest();
    run(&pm, &in, 200, &now);
    CHECK(count(EV_MOVE) == 0);

    // A stalled step counts as 50 ms, not the whole pause
    clearEvents();
    in = rest();
    in.leftX = STICK;
    now += 10 * MS;
    padMouseStep(&pm, &in, 1, now, &SINK);
    clearEvents();
    now += 5000 * MS;
    padMouseStep(&pm, &in, 1, now, &SINK);
    CHECK(sumDx >= 88 && sumDx <= 92);
    // The clock going backwards moves nothing
    clearEvents();
    now -= 1000 * MS;
    padMouseStep(&pm, &in, 1, now, &SINK);
    CHECK(sumDx == 0);
}

static void testScroll(void) {
    PadMouse pm;
    padMouseInit(&pm);
    int64_t now = 0;
    turnOn(&pm, &now);

    // The right stick up for a second: ten notches up, no sideways, the pointer still
    clearEvents();
    PadMouseIn in = rest();
    in.rightY = STICK;
    run(&pm, &in, 1000, &now);
    CHECK(sumScroll >= 1190 && sumScroll <= 1210);
    CHECK(sumHScroll == 0 && sumDx == 0 && sumDy == 0);
    // Down is negative
    clearEvents();
    in = rest();
    in.rightY = -STICK;
    run(&pm, &in, 500, &now);
    CHECK(sumScroll <= -590 && sumScroll >= -610);
    // Right is the horizontal wheel
    clearEvents();
    in = rest();
    in.rightX = STICK;
    run(&pm, &in, 500, &now);
    CHECK(sumHScroll >= 590 && sumHScroll <= 610 && sumScroll == 0);
    // Pushed up and a little across: the leaning axis only
    clearEvents();
    in = rest();
    in.rightY = STICK;
    in.rightX = (int)(0.4f * 32767.0f);
    run(&pm, &in, 500, &now);
    CHECK(sumScroll > 500 && sumHScroll == 0);
    // Gentle is slow
    clearEvents();
    in = rest();
    in.rightY = (int)(0.4f * 32767.0f);   // travel 0.29
    run(&pm, &in, 1000, &now);
    CHECK(sumScroll > 50 && sumScroll < 150);

    // The left stick scrolls, the right one points
    padMouseConfigure(&pm, PAD_MOUSE_CHORD_START_HOLD, PAD_MOUSE_SCROLL_LEFT);
    clearEvents();
    in = rest();
    in.leftY = STICK;
    in.rightX = STICK;
    run(&pm, &in, 100, &now);
    CHECK(sumScroll > 100 && sumDx >= 178 && sumDx <= 182 && sumDy == 0);

    // None: the left stick points and the right one is idle
    padMouseConfigure(&pm, PAD_MOUSE_CHORD_START_HOLD, PAD_MOUSE_SCROLL_NONE);
    clearEvents();
    in = rest();
    in.leftX = STICK;
    in.rightY = STICK;
    in.rightX = STICK;
    run(&pm, &in, 100, &now);
    CHECK(sumDx >= 178 && sumScroll == 0 && sumHScroll == 0);
}

static void testReleaseAll(void) {
    PadMouse pm;
    padMouseInit(&pm);
    int64_t now = 0;
    turnOn(&pm, &now);
    PadMouseIn in = held(PAD_A | PAD_RB | PAD_UP | PAD_LEFT);
    in.leftX = STICK;
    run(&pm, &in, 50, &now);
    CHECK(pm.downMask != 0);

    // Focus lost: everything up, the mode off, no toast, rest for the game
    clearEvents();
    PadMouseOut out = padMouseStep(&pm, &in, 0, now + 10 * MS, &SINK);
    CHECK(!out.neutral && out.strip == 0 && !pm.on && pm.downMask == 0);
    CHECK(count(EV_BUTTON) == 2 && heard(EV_BUTTON, PADMOUSE_BTN_LEFT, 0) && heard(EV_BUTTON, PADMOUSE_BTN_X2, 0));
    CHECK(count(EV_KEY) == 2 && heard(EV_KEY, 0x26, 0) && heard(EV_KEY, 0x25, 0));
    CHECK(count(EV_MODE) == 0);
    CHECK(!padMouseWantsTick(&pm));
    // Nothing more while unfocused, and the same call again says nothing
    clearEvents();
    padMouseStep(&pm, &in, 0, now + 20 * MS, &SINK);
    CHECK(evCount == 0);
    // Back in focus with the buttons still held: a plain pad again, the game gets them
    out = padMouseStep(&pm, &in, 1, now + 30 * MS, &SINK);
    CHECK(!out.neutral && out.strip == 0 && evCount == 0);

    // Release on the spot (stream end, pad gone), with a held stick leaving no remainder
    now += 100 * MS;
    padMouseRelease(&pm, &SINK);
    turnOn(&pm, &now);
    in = held(PAD_B);
    in.leftX = (int)(0.3f * 32767.0f);
    run(&pm, &in, 30, &now);
    CHECK(pm.downMask != 0);
    clearEvents();
    padMouseRelease(&pm, &SINK);
    CHECK(!pm.on && pm.downMask == 0 && pm.remX == 0.0f && pm.remY == 0.0f);
    CHECK(count(EV_BUTTON) == 1 && heard(EV_BUTTON, PADMOUSE_BTN_RIGHT, 0) && count(EV_MODE) == 0);
    // Releasing nothing sends nothing
    clearEvents();
    padMouseRelease(&pm, &SINK);
    CHECK(evCount == 0);

    // The toggle itself lets go (a button down when Start ends the mode)
    turnOn(&pm, &now);
    PadMouseIn start = held(PAD_START);
    run(&pm, &start, 100, &now);
    PadMouseIn startRb = held(PAD_START | PAD_RB);
    run(&pm, &startRb, 30, &now);
    CHECK(heard(EV_BUTTON, PADMOUSE_BTN_X2, 1));
    clearEvents();
    PadMouseIn startOnly = held(PAD_START);
    run(&pm, &startOnly, 1100, &now);
    CHECK(!pm.on && pm.downMask == 0);
    CHECK(heard(EV_BUTTON, PADMOUSE_BTN_X2, 0) || count(EV_BUTTON) == 0);
}

static void testNoSink(void) {
    // Every hook is optional
    PadMouse pm;
    padMouseInit(&pm);
    int64_t now = 0;
    PadMouseIn start = held(PAD_START);
    PadMouseOut out = { 0, 0 };
    for (int t = 0; t < 1100; t += 10) {
        now += 10 * MS;
        out = padMouseStep(&pm, &start, 1, now, NULL);
    }
    CHECK(pm.on && out.neutral);
    PadMouseSink empty;
    memset(&empty, 0, sizeof(empty));
    PadMouseIn in = held(PAD_A | PAD_UP | PAD_Y);
    in.leftX = STICK;
    in.rightY = STICK;
    for (int t = 0; t < 100; t += 10) {
        now += 10 * MS;
        padMouseStep(&pm, &in, 1, now, &empty);
    }
    padMouseRelease(&pm, NULL);
    CHECK(!pm.on);
}

static void testNoticeText(void) {
    CHECK(strstr(padMouseNoticeText(1, PAD_MOUSE_CHORD_START_HOLD), "hold Start to give") != NULL);
    CHECK(strstr(padMouseNoticeText(1, PAD_MOUSE_CHORD_START_BACK), "Start and Back") != NULL);
    CHECK(strstr(padMouseNoticeText(1, PAD_MOUSE_CHORD_OFF), "mouse") != NULL);
    CHECK(strstr(padMouseNoticeText(0, PAD_MOUSE_CHORD_START_HOLD), "gamepad again") != NULL);
    // Plain ASCII, one short line (the toast's font)
    const int chords[] = { PAD_MOUSE_CHORD_START_HOLD, PAD_MOUSE_CHORD_START_BACK, PAD_MOUSE_CHORD_OFF };
    for (int c = 0; c < 3; c++)
        for (int on = 0; on < 2; on++) {
            const char* t = padMouseNoticeText(on, chords[c]);
            int ok = strlen(t) > 0 && strlen(t) < 70;
            for (const char* p = t; *p; p++) ok &= *p >= 32 && *p < 127;
            CHECK(ok);
        }
}

int main(void) {
    testCurve();
    testChordTiming();
    testChordChoices();
    testButtonsAndKeys();
    testHeldAtToggle();
    testPointer();
    testScroll();
    testReleaseAll();
    testNoSink();
    testNoticeText();
    return checksDone("padmouse");
}
