// Controller bindings: the defaults against today's fixed maps, bindings.ini
// read strictly, per app layers, the reserved chords, the 5-button mouse map
// and the files written and read back. New in this fork.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "check.h"
#include "xr_bind.h"
#include "xr_pressfix.h"

#define S(name) XR_BIND_BIT(XR_BIND_SRC_##name)

static XrBindSet g_set;
static XrBindSet g_set2;
static char g_text[32768];
static char g_text2[32768];

static XrBindAction act(int kind, int code, int mods) {
    XrBindAction a = { kind, code, mods };
    return a;
}

static int actIs(const XrBindAction* a, int kind, int code, int mods) {
    XrBindAction b = act(kind, code, mods);
    return xrBindActionEqual(a, &b);
}

static int tablesSameActions(const XrBindTable* a, const XrBindTable* b) {
    if (a->mode != b->mode || a->chordCount != b->chordCount) {
        return 0;
    }
    for (int i = 0; i < XR_BIND_SRC_COUNT; i++) {
        if (!xrBindActionEqual(&a->single[i], &b->single[i])) {
            return 0;
        }
    }
    for (int i = 0; i < a->chordCount; i++) {
        if (a->chords[i].parts != b->chords[i].parts
                || !xrBindActionEqual(&a->chords[i].action, &b->chords[i].action)) {
            return 0;
        }
    }
    return 1;
}

static void testNames(void) {
    for (int i = 0; i < XR_BIND_SRC_COUNT; i++) {
        CHECK(xrBindSourceName(i) != NULL);
        CHECK(xrBindSourceFind(xrBindSourceName(i)) == i);
    }
    CHECK(xrBindSourceName(XR_BIND_SRC_COUNT) == NULL);
    CHECK(xrBindSourceFind("RIGHT.Y") == XR_BIND_SRC_RIGHT_Y);
    CHECK(xrBindSourceFind("right.system") == -1);
    CHECK(xrBindSourceIsStick(XR_BIND_SRC_LEFT_STICK));
    CHECK(!xrBindSourceIsStick(XR_BIND_SRC_LEFT_STICK_UP));

    CHECK(xrBindKeyFind("F") == 0x46);
    CHECK(xrBindKeyFind("f") == 0x46);
    CHECK(xrBindKeyFind("esc") == 0x1B);
    CHECK(xrBindKeyFind("Escape") == 0x1B);
    CHECK(xrBindKeyFind("f12") == 0x7B);
    CHECK(xrBindKeyFind("space") == 0x20);
    CHECK(xrBindKeyFind("pagedown") == 0x22);
    CHECK(xrBindKeyFind("0x41") == 0x41);
    CHECK(xrBindKeyFind("vk65") == 65);
    CHECK(xrBindKeyFind("0x100") == 0);
    CHECK(xrBindKeyFind("0x") == 0);
    CHECK(xrBindKeyFind("nope") == 0);
    CHECK(strcmp(xrBindKeyName(0x46), "F") == 0);
    CHECK(strcmp(xrBindKeyName(0x1B), "esc") == 0);
    CHECK(xrBindKeyName(0xE9) == NULL);

    // Every action the grammar has, out and back
    static const char* const TEXTS[] = {
        "none", "key:F", "key:ctrl+shift+esc", "key:win+D", "key:0xE9", "mouse:left",
        "mouse:x1", "mouse:x2", "wheel:up", "wheel:right", "pad:a", "pad:lt", "pad:guide",
        "pad:dpad_left", "stick:scroll", "stick:wasd", "stick:pad_right", "app:keyboard",
        "app:ray", "app:recentre", "app:cog", "app:3d", "app:gamepad", "app:bringhere", "app:host",
        "app:zoomin", "app:zoomout", "app:zoomreset",
    };
    for (size_t i = 0; i < sizeof(TEXTS) / sizeof(TEXTS[0]); i++) {
        XrBindAction a, b;
        char out[64], err[160];
        CHECK(xrBindActionParse(TEXTS[i], &a, err, sizeof(err), NULL));
        xrBindActionFormat(&a, out, sizeof(out));
        CHECK(strcmp(out, TEXTS[i]) == 0);
        CHECK(xrBindActionParse(out, &b, err, sizeof(err), NULL));
        CHECK(xrBindActionEqual(&a, &b));
    }
    XrBindAction a;
    char err[160];
    int col = -1;
    CHECK(xrBindActionParse("KEY:Alt+F4", &a, err, sizeof(err), &col));
    CHECK(actIs(&a, XR_BIND_ACT_KEY, 0x73, XR_BIND_MOD_ALT));
    // PLAN 4b.6: the zoom actions, the codes xr_input.c's bindAppCommand switches on
    CHECK(xrBindActionParse("app:zoomin", &a, err, sizeof(err), &col));
    CHECK(actIs(&a, XR_BIND_ACT_APP, XR_BIND_APP_ZOOMIN, 0));
    CHECK(xrBindActionParse("APP:ZoomOut", &a, err, sizeof(err), &col));
    CHECK(actIs(&a, XR_BIND_ACT_APP, XR_BIND_APP_ZOOMOUT, 0));
    CHECK(xrBindActionParse("app:zoomreset", &a, err, sizeof(err), &col));
    CHECK(actIs(&a, XR_BIND_ACT_APP, XR_BIND_APP_ZOOMRESET, 0));
    CHECK(!xrBindActionParse("app:zoom", &a, err, sizeof(err), &col));
    CHECK(strstr(err, "zoomreset") != NULL);
    CHECK(xrBindActionParse("app:none", &a, err, sizeof(err), &col));
    CHECK(a.kind == XR_BIND_ACT_NONE);
    CHECK(!xrBindActionParse("key:F+ctrl", &a, err, sizeof(err), &col));
    CHECK(col == 4);
    CHECK(strstr(err, "not a modifier") != NULL);
    CHECK(!xrBindActionParse("mouse:x3", &a, err, sizeof(err), &col));
    CHECK(col == 6);
    CHECK(!xrBindActionParse("pad:paddle1", &a, err, sizeof(err), &col));
    CHECK(!xrBindActionParse("left", &a, err, sizeof(err), &col));
    CHECK(!xrBindActionParse("key:", &a, err, sizeof(err), &col));
    CHECK(col == 4);
}

// The 3-button MAP widened to 5: X1 and X2 after it, and VR_BUTTON_ bits that
// go on from LEFT 1, RIGHT 2, MIDDLE 4
static void testMouseMap(void) {
    static const int TODAY[3] = { 1, 3, 2 };
    for (int i = 0; i < 3; i++) {
        CHECK(xrBindHostButton(i) == TODAY[i]);
    }
    CHECK(xrBindHostButton(XR_BIND_MOUSE_X1) == 4);
    CHECK(xrBindHostButton(XR_BIND_MOUSE_X2) == 5);
    CHECK(xrBindHostButton(-1) == 0);
    CHECK(xrBindHostButton(XR_BIND_MOUSE_BUTTONS) == 0);
    CHECK((1 << XR_BIND_MOUSE_LEFT) == 1);
    CHECK((1 << XR_BIND_MOUSE_RIGHT) == 2);
    CHECK((1 << XR_BIND_MOUSE_MIDDLE) == 4);

    XrBindTable t;
    xrBindResolve(NULL, XR_BIND_MODE_POINTER, NULL, &t);
    CHECK(xrBindMouseMask(&t, S(LEFT_BUMPER)) == 8);
    CHECK(xrBindMouseMask(&t, S(RIGHT_BUMPER)) == 16);
    CHECK(xrBindMouseMask(&t, S(LEFT_BUMPER) | S(RIGHT_BUMPER) | S(RIGHT_TRIGGER)) == 25);
}

// Pointer mode's defaults click as pointerButtonMask does today, over every
// combination of the controls it reads; the seven idle controls get theirs
static void testPointerDefaults(void) {
    XrBindTable t;
    xrBindResolve(NULL, XR_BIND_MODE_POINTER, NULL, &t);
    CHECK(t.mode == XR_BIND_MODE_POINTER);
    int wrong = 0;
    for (int m = 0; m < 256; m++) {
        int bit[8];
        for (int b = 0; b < 8; b++) {
            bit[b] = (m >> b) & 1;
        }
        PointerButtons pb;
        memset(&pb, 0, sizeof(pb));
        pb.triggerDown[0] = bit[0];
        pb.triggerDown[1] = bit[1];
        pb.rightButton = bit[2] || bit[3];
        pb.middleButton = bit[4] || bit[5];
        pb.stickClick[0] = bit[6];
        pb.stickClick[1] = bit[7];
        pb.stickRight = 1;
        uint64_t down = (bit[0] ? S(LEFT_TRIGGER) : 0) | (bit[1] ? S(RIGHT_TRIGGER) : 0)
                        | (bit[2] ? S(RIGHT_A) : 0) | (bit[3] ? S(LEFT_DPAD_DOWN) : 0)
                        | (bit[4] ? S(RIGHT_B) : 0) | (bit[5] ? S(LEFT_DPAD_RIGHT) : 0)
                        | (bit[6] ? S(LEFT_STICK_CLICK) : 0) | (bit[7] ? S(RIGHT_STICK_CLICK) : 0);
        int want = pointerButtonMask(&pb);
        int got = xrBindMouseMask(&t, down);
        if (got != want) {
            if (wrong == 0) {
                fprintf(stderr, "pointer combo %#x: %d, today %d\n", m, got, want);
            }
            wrong++;
        }
    }
    CHECK(wrong == 0);

    // The seven controls pointer mode left idle
    CHECK(actIs(&t.single[XR_BIND_SRC_RIGHT_Y], XR_BIND_ACT_APP, XR_BIND_APP_KEYBOARD, 0));
    CHECK(actIs(&t.single[XR_BIND_SRC_RIGHT_X], XR_BIND_ACT_APP, XR_BIND_APP_3D, 0));
    CHECK(actIs(&t.single[XR_BIND_SRC_LEFT_DPAD_UP], XR_BIND_ACT_APP, XR_BIND_APP_RECENTRE, 0));
    CHECK(actIs(&t.single[XR_BIND_SRC_RIGHT_MENU], XR_BIND_ACT_APP, XR_BIND_APP_COG, 0));
    CHECK(actIs(&t.single[XR_BIND_SRC_LEFT_BUMPER], XR_BIND_ACT_MOUSE, XR_BIND_MOUSE_X1, 0));
    CHECK(actIs(&t.single[XR_BIND_SRC_RIGHT_BUMPER], XR_BIND_ACT_MOUSE, XR_BIND_MOUSE_X2, 0));
    CHECK(t.single[XR_BIND_SRC_LEFT_DPAD_LEFT].kind == XR_BIND_ACT_NONE);
    // The rest as they were: sticks scroll, grips and View do nothing of their own
    CHECK(actIs(&t.single[XR_BIND_SRC_LEFT_STICK], XR_BIND_ACT_STICK, XR_BIND_STICK_SCROLL, 0));
    CHECK(actIs(&t.single[XR_BIND_SRC_RIGHT_STICK], XR_BIND_ACT_STICK, XR_BIND_STICK_SCROLL, 0));
    CHECK(t.single[XR_BIND_SRC_LEFT_GRIP].kind == XR_BIND_ACT_NONE);
    CHECK(t.single[XR_BIND_SRC_RIGHT_GRIP].kind == XR_BIND_ACT_NONE);
    CHECK(t.single[XR_BIND_SRC_LEFT_VIEW].kind == XR_BIND_ACT_NONE);
    CHECK(t.chordCount == 0);
    for (int i = 0; i < XR_BIND_SRC_COUNT; i++) {
        CHECK(t.from[i] == XR_BIND_FROM_DEFAULT);
    }

    // The app commands come out as they are held
    XrBindAction held[8];
    int n = xrBindHeld(&t, S(RIGHT_Y) | S(LEFT_STICK), held, 8);
    CHECK(n == 1);
    CHECK(actIs(&held[0], XR_BIND_ACT_APP, XR_BIND_APP_KEYBOARD, 0));
}

static PadHand frameHand(void) {
    PadHand h;
    memset(&h, 0, sizeof(h));
    h.frame = 1;
    return h;
}

// Gamepad mode's defaults make the buttons padMap makes today, over every
// combination of the buttons, with Guide where padGuideStep would give it
// once held
static void testGamepadDefaults(void) {
    XrBindTable t;
    xrBindResolve(NULL, XR_BIND_MODE_GAMEPAD, NULL, &t);
    CHECK(t.chordCount == 1);
    CHECK(t.chords[0].parts == (S(LEFT_VIEW) | S(RIGHT_MENU)));
    CHECK(actIs(&t.chords[0].action, XR_BIND_ACT_PAD, PAD_GUIDE, 0));
    CHECK(actIs(&t.single[XR_BIND_SRC_LEFT_TRIGGER], XR_BIND_ACT_PAD, PAD_HELD_LT, 0));
    CHECK(actIs(&t.single[XR_BIND_SRC_RIGHT_TRIGGER], XR_BIND_ACT_PAD, PAD_HELD_RT, 0));
    CHECK(actIs(&t.single[XR_BIND_SRC_LEFT_STICK], XR_BIND_ACT_STICK, XR_BIND_STICK_PAD_LEFT, 0));
    CHECK(actIs(&t.single[XR_BIND_SRC_RIGHT_STICK], XR_BIND_ACT_STICK, XR_BIND_STICK_PAD_RIGHT, 0));

    int wrong = 0;
    for (int m = 0; m < 65536; m++) {
        PadHand l = frameHand(), r = frameHand();
        int b = m;
        l.lower = b & 1; b >>= 1;
        l.upper = b & 1; b >>= 1;
        l.menu = b & 1; b >>= 1;
        l.dpadUp = b & 1; b >>= 1;
        l.dpadLeft = b & 1; b >>= 1;
        l.bumper = b & 1; b >>= 1;
        l.stickClick = b & 1; b >>= 1;
        l.grip = (float)(b & 1); b >>= 1;
        r.lower = b & 1; b >>= 1;
        r.upper = b & 1; b >>= 1;
        r.faceX = b & 1; b >>= 1;
        r.faceY = b & 1; b >>= 1;
        r.start = b & 1; b >>= 1;
        r.bumper = b & 1; b >>= 1;
        r.stickClick = b & 1; b >>= 1;
        r.grip = (float)(b & 1);
        int gripDown[2] = { padGripDown(l.grip, 0), padGripDown(r.grip, 0) };
        PadState ps;
        padMap(&l, &r, gripDown, 1, PAD_STICK_DEADZONE_DEFAULT, &ps);
        int want = ps.buttons;
        if (l.menu && r.start && !(want & (PAD_LB | PAD_RB))) {
            want = (want & ~(PAD_START | PAD_BACK)) | PAD_GUIDE;
        }
        int got = xrBindPadButtons(&t, xrBindSourcesDown(&l, &r, 0));
        if (got != want) {
            if (wrong == 0) {
                fprintf(stderr, "pad combo %#x: %#x, today %#x\n", m, got, want);
            }
            wrong++;
        }
    }
    CHECK(wrong == 0);
}

// The disconnect chord works whatever its four controls are bound to
static void testDisconnect(void) {
    const char* text = "[gamepad]\n"
                       "left.bumper = key:Q\n"
                       "right.bumper = key:E\n"
                       "right.menu = key:esc\n";
    CHECK(xrBindParse(&g_set, text, NULL) == 0);
    XrBindTable t;
    xrBindResolve(&g_set, XR_BIND_MODE_GAMEPAD, NULL, &t);
    uint64_t chord = S(LEFT_VIEW) | S(RIGHT_MENU) | S(LEFT_BUMPER) | S(RIGHT_BUMPER);
    CHECK(xrBindPadButtons(&t, chord) == (PAD_START | PAD_BACK | PAD_LB | PAD_RB));
    XrBindAction held[8];
    int n = xrBindHeld(&t, chord, held, 8);
    CHECK(n == 1);
    // One of them alone is what it is bound to
    n = xrBindHeld(&t, S(LEFT_BUMPER), held, 8);
    CHECK(n == 1);
    CHECK(actIs(&held[0], XR_BIND_ACT_KEY, 'Q', 0));
    // In pointer mode there is no disconnect chord
    xrBindResolve(&g_set, XR_BIND_MODE_POINTER, NULL, &t);
    CHECK(xrBindPadButtons(&t, chord) == 0);
}

static const char* SAMPLE =
    "\xEF\xBB\xBF; a sample, with an editor's byte order mark and Windows line ends\r\n"
    "[pointer]                 ; all apps, pointer mode\r\n"
    "right.x = key:F\r\n"
    "right.y = key:space\n"
    "left.dpad_up = key:up\n"
    "  left.bumper   =   mouse:x1   \n"
    "right.bumper = mouse:x2 # inline comment\n"
    "left.stick_left = key:ctrl+shift+esc\n"
    "right.stick = stick:mouse\n"
    "left.dpad_left + right.x_touch = wheel:down\n"
    "\n"
    "[gamepad]                 ; gamepad mode overrides\n"
    "left.grip = none          ; free the grips from LB/RB\n"
    "right.grip = key:R\n"
    "left.dpad_left = wheel:up\n"
    "left.view+right.menu = none\n"
    "right.a+right.b = pad:guide\n"
    "\n"
    "[app \"Time Crisis 2\"]\n"
    "pointer.right.trigger = mouse:left\n"
    "pointer.left.dpad_left = key:0x52\n"
    "gamepad.right.grip = pad:rb\n"
    "Pointer.Right.X = app:none\n"
    "\n"
    "[ app \"Other Game\" ]\n"
    "gamepad.left.view + right.menu = pad:guide\n";

static void testParseSample(void) {
    XrBindErrors errs;
    int n = xrBindParse(&g_set, SAMPLE, &errs);
    CHECK(n == 0);
    if (n > 0) {
        fprintf(stderr, "first error: %s\n", errs.errors[0].text);
    }
    CHECK(g_set.ruleCount == 18);
    CHECK(g_set.appCount == 2);
    CHECK(strcmp(g_set.apps[0], "Time Crisis 2") == 0);
    CHECK(strcmp(g_set.apps[1], "Other Game") == 0);

    XrBindTable t;
    xrBindResolve(&g_set, XR_BIND_MODE_POINTER, NULL, &t);
    CHECK(actIs(&t.single[XR_BIND_SRC_RIGHT_X], XR_BIND_ACT_KEY, 'F', 0));
    CHECK(t.from[XR_BIND_SRC_RIGHT_X] == XR_BIND_FROM_MODE);
    CHECK(actIs(&t.single[XR_BIND_SRC_RIGHT_Y], XR_BIND_ACT_KEY, 0x20, 0));
    CHECK(actIs(&t.single[XR_BIND_SRC_LEFT_DPAD_UP], XR_BIND_ACT_KEY, 0x26, 0));
    CHECK(actIs(&t.single[XR_BIND_SRC_LEFT_STICK_LEFT], XR_BIND_ACT_KEY, 0x1B,
                XR_BIND_MOD_CTRL | XR_BIND_MOD_SHIFT));
    CHECK(actIs(&t.single[XR_BIND_SRC_RIGHT_STICK], XR_BIND_ACT_STICK, XR_BIND_STICK_MOUSE, 0));
    CHECK(actIs(&t.single[XR_BIND_SRC_RIGHT_TRIGGER], XR_BIND_ACT_MOUSE, XR_BIND_MOUSE_LEFT, 0));
    CHECK(t.from[XR_BIND_SRC_RIGHT_TRIGGER] == XR_BIND_FROM_DEFAULT);
    CHECK(t.chordCount == 1);
    CHECK(t.chords[0].parts == (S(LEFT_DPAD_LEFT) | S(RIGHT_X_TOUCH)));
    CHECK(actIs(&t.chords[0].action, XR_BIND_ACT_WHEEL, XR_BIND_WHEEL_DOWN, 0));

    // The chord takes its parts' own actions out while it is held
    XrBindAction held[8];
    int h = xrBindHeld(&t, S(LEFT_DPAD_LEFT) | S(RIGHT_X_TOUCH) | S(RIGHT_X), held, 8);
    CHECK(h == 2);
    CHECK(actIs(&held[0], XR_BIND_ACT_WHEEL, XR_BIND_WHEEL_DOWN, 0));
    CHECK(actIs(&held[1], XR_BIND_ACT_KEY, 'F', 0));

    xrBindResolve(&g_set, XR_BIND_MODE_GAMEPAD, NULL, &t);
    CHECK(t.single[XR_BIND_SRC_LEFT_GRIP].kind == XR_BIND_ACT_NONE);
    CHECK(t.from[XR_BIND_SRC_LEFT_GRIP] == XR_BIND_FROM_MODE);
    CHECK(actIs(&t.single[XR_BIND_SRC_RIGHT_GRIP], XR_BIND_ACT_KEY, 'R', 0));
    CHECK(actIs(&t.single[XR_BIND_SRC_LEFT_DPAD_LEFT], XR_BIND_ACT_WHEEL, XR_BIND_WHEEL_UP, 0));
    // Guide moved from View+Menu to A+B
    CHECK(t.chordCount == 1);
    CHECK(t.chords[0].parts == (S(RIGHT_A) | S(RIGHT_B)));
    CHECK(xrBindPadButtons(&t, S(LEFT_VIEW) | S(RIGHT_MENU)) == (PAD_BACK | PAD_START));
    CHECK(xrBindPadButtons(&t, S(RIGHT_A) | S(RIGHT_B)) == PAD_GUIDE);

    // The app's layer, its name matched case blind, over the mode's
    xrBindResolve(&g_set, XR_BIND_MODE_POINTER, "time crisis 2", &t);
    CHECK(actIs(&t.single[XR_BIND_SRC_LEFT_DPAD_LEFT], XR_BIND_ACT_KEY, 0x52, 0));
    CHECK(t.from[XR_BIND_SRC_LEFT_DPAD_LEFT] == XR_BIND_FROM_APP);
    CHECK(t.single[XR_BIND_SRC_RIGHT_X].kind == XR_BIND_ACT_NONE);
    CHECK(t.from[XR_BIND_SRC_RIGHT_X] == XR_BIND_FROM_APP);
    CHECK(actIs(&t.single[XR_BIND_SRC_RIGHT_TRIGGER], XR_BIND_ACT_MOUSE, XR_BIND_MOUSE_LEFT, 0));
    CHECK(t.from[XR_BIND_SRC_RIGHT_TRIGGER] == XR_BIND_FROM_APP);
    CHECK(actIs(&t.single[XR_BIND_SRC_RIGHT_Y], XR_BIND_ACT_KEY, 0x20, 0));
    xrBindResolve(&g_set, XR_BIND_MODE_GAMEPAD, "Time Crisis 2", &t);
    CHECK(actIs(&t.single[XR_BIND_SRC_RIGHT_GRIP], XR_BIND_ACT_PAD, PAD_RB, 0));
    CHECK(t.single[XR_BIND_SRC_LEFT_GRIP].kind == XR_BIND_ACT_NONE);
    // An app with no section of its own, and the other app's chord back
    xrBindResolve(&g_set, XR_BIND_MODE_GAMEPAD, "Somebody Else", &t);
    CHECK(actIs(&t.single[XR_BIND_SRC_RIGHT_GRIP], XR_BIND_ACT_KEY, 'R', 0));
    xrBindResolve(&g_set, XR_BIND_MODE_GAMEPAD, "Other Game", &t);
    CHECK(t.chordCount == 2);
    CHECK(xrBindPadButtons(&t, S(LEFT_VIEW) | S(RIGHT_MENU)) == PAD_GUIDE);
}

// One bad line among good ones: one error, where it is, and the rest read
static void expectError(const char* text, int line, int col, const char* words, int rules) {
    XrBindErrors errs;
    int n = xrBindParse(&g_set, text, &errs);
    CHECK(n == 1);
    CHECK(errs.count == 1);
    CHECK(g_set.ruleCount == rules);
    if (n >= 1) {
        CHECK(errs.errors[0].line == line);
        CHECK(errs.errors[0].column == col);
        CHECK(strstr(errs.errors[0].text, words) != NULL);
        char where[32];
        snprintf(where, sizeof(where), "%d:%d: ", line, col);
        CHECK(strncmp(errs.errors[0].text, where, strlen(where)) == 0);
        if (errs.errors[0].line != line || errs.errors[0].column != col
                || strstr(errs.errors[0].text, words) == NULL) {
            fprintf(stderr, "got: %s\n", errs.errors[0].text);
        }
    }
}

static void testErrors(void) {
    // The reserved chords
    expectError("[pointer]\nleft.view+left.grip = key:A\nright.x = key:F\n", 2, 1, "switch", 1);
    expectError("[gamepad]\nright.stick_click + left.stick_click = pad:guide\n", 2, 1, "switch", 0);
    expectError("[gamepad]\nleft.trigger+right.trigger+left.grip+right.grip = key:A\n", 2, 1,
                "switch", 0);
    expectError("[app \"X\"]\ngamepad.left.grip+left.view = key:A\n", 2, 9, "switch", 0);
    expectError("[gamepad]\nleft.view+right.menu+left.bumper+right.bumper = key:Q\n", 2, 1,
                "disconnect", 0);
    expectError("[pointer]\nleft.grip+right.a = key:A\n", 2, 1, "grip", 0);
    // ... which gamepad mode allows
    CHECK(xrBindParse(&g_set, "[gamepad]\nleft.grip+right.a = key:A\n", NULL) == 0);

    // Unknown things
    expectError("[pointer]\nright.x = kye:F\n", 2, 11, "unknown action kind", 0);
    expectError("[pointer]\nright.x = key:ctrl+foo\nright.y = key:G\n", 2, 20, "unknown key", 1);
    expectError("[pointer]\nrigth.x = key:F\n", 2, 1, "unknown control", 0);
    expectError("[pointer]\nright.a + rihgt.b = key:F\n", 2, 11, "unknown control", 0);
    expectError("[pointer]\nright.system = key:F\n", 2, 1, "SteamVR", 0);
    expectError("[pointr]\nright.x = key:F\n[pointer]\nright.y = key:G\n", 1, 2, "unknown section",
                1);
    expectError("right.x = key:F\n[pointer]\nright.y = key:G\n", 1, 1, "before any section", 1);
    expectError("[app Name]\n", 1, 6, "quotes", 0);
    expectError("[app \"Name]\n", 1, 6, "closing quote", 0);
    expectError("[pointer] x\nright.y = key:G\n", 1, 11, "after the section", 1);

    // Duplicates: the first line stands
    expectError("[pointer]\nright.x = key:F\nright.x = key:G\n", 3, 1, "already bound on line 2",
                1);
    XrBindTable t;
    xrBindResolve(&g_set, XR_BIND_MODE_POINTER, NULL, &t);
    CHECK(actIs(&t.single[XR_BIND_SRC_RIGHT_X], XR_BIND_ACT_KEY, 'F', 0));
    expectError("[gamepad]\nright.a+right.b = pad:guide\nright.b+right.a = key:G\n", 3, 1,
                "already bound", 1);
    expectError("[pointer]\nright.a+right.a = key:G\n", 2, 9, "twice", 0);
    // The same control in two sections, or a section split in two, is fine
    // apart from the duplicate
    CHECK(xrBindParse(&g_set, "[pointer]\nright.x = key:F\n[gamepad]\nright.x = key:F\n", NULL) == 0);
    expectError("[pointer]\nright.x = key:F\n[gamepad]\n[pointer]\nright.x = key:G\n", 5, 1,
                "already bound", 1);

    // Actions in the wrong place
    expectError("[pointer]\nright.x = pad:a\n", 2, 11, "gamepad mode", 0);
    expectError("[pointer]\nleft.stick = stick:pad_left\n", 2, 14, "gamepad mode", 0);
    expectError("[pointer]\nright.x = stick:scroll\n", 2, 11, "left.stick and right.stick", 0);
    expectError("[gamepad]\nleft.stick = key:A\n", 2, 14, "stick:", 0);
    expectError("[gamepad]\nleft.stick+right.a = pad:a\n", 2, 1, "whole stick", 0);
    expectError("[gamepad]\nright.a+right.b+right.x+right.y+right.menu = pad:a\n", 2, 33,
                "at most 4", 0);

    // The mode prefix: needed in an [app] section, nowhere else
    expectError("[app \"X\"]\nright.x = key:F\n", 2, 1, "pointer. or gamepad.", 0);
    expectError("[pointer]\npointer.right.x = key:F\n", 2, 1, "[app] sections", 0);

    // Lines that are not lines
    expectError("[pointer]\nright.x key:F\n", 2, 1, "control = action", 0);
    expectError("[pointer]\nright.x =\n", 2, 10, "no action", 0);
    expectError("[pointer]\n= key:F\n", 2, 1, "no control", 0);
    char longLine[XR_BIND_LINE_MAX + 64];
    memset(longLine, 0, sizeof(longLine));
    strcpy(longLine, "[pointer]\nright.x = key:F");
    size_t at = strlen(longLine);
    memset(longLine + at, ' ', XR_BIND_LINE_MAX);
    longLine[at + XR_BIND_LINE_MAX] = '\n';
    expectError(longLine, 2, XR_BIND_LINE_MAX + 1, "longer", 0);

    // Errors past what is kept are still counted
    char many[4096];
    size_t len = (size_t)snprintf(many, sizeof(many), "[pointer]\n");
    for (int i = 0; i < 40; i++) {
        len += (size_t)snprintf(many + len, sizeof(many) - len, "nope%d = key:F\n", i);
    }
    XrBindErrors errs;
    CHECK(xrBindParse(&g_set, many, &errs) == 40);
    CHECK(errs.count == 40);
    CHECK(errs.errors[XR_BIND_MAX_ERRORS - 1].line == XR_BIND_MAX_ERRORS + 1);

    // Nothing at all, and junk, without a crash
    CHECK(xrBindParse(&g_set, "", NULL) == 0);
    CHECK(xrBindParse(&g_set, NULL, NULL) == 0);
    char junk[2048];
    unsigned seed = 12345;
    for (int round = 0; round < 50; round++) {
        for (size_t i = 0; i + 1 < sizeof(junk); i++) {
            seed = seed * 1103515245u + 12345u;
            char c = (char)((seed >> 16) & 0x7F);
            junk[i] = c == 0 ? '\n' : c;
        }
        junk[sizeof(junk) - 1] = '\0';
        xrBindParse(&g_set, junk, &errs);
        CHECK(g_set.ruleCount <= XR_BIND_MAX_RULES);
    }
}

// Takes the # off every default line of the default file
static void uncomment(const char* in, char* out, size_t cap) {
    size_t o = 0;
    int lineStart = 1;
    for (const char* p = in; *p != '\0' && o + 1 < cap; p++) {
        if (lineStart && p[0] == '#' && ((p[1] >= 'a' && p[1] <= 'z'))) {
            lineStart = 0;
            continue;
        }
        lineStart = *p == '\n';
        out[o++] = *p;
    }
    out[o] = '\0';
}

static void testRoundTrip(void) {
    XrBindTable want[2], got[2];
    xrBindResolve(NULL, XR_BIND_MODE_POINTER, NULL, &want[0]);
    xrBindResolve(NULL, XR_BIND_MODE_GAMEPAD, NULL, &want[1]);

    // The first run file changes nothing as written...
    int len = xrBindDefaultText(NULL, 0);
    CHECK(len > 0 && len < (int)sizeof(g_text));
    CHECK(xrBindDefaultText(g_text, sizeof(g_text)) == len);
    CHECK((int)strlen(g_text) == len);
    XrBindErrors errs;
    CHECK(xrBindParse(&g_set, g_text, &errs) == 0);
    CHECK(g_set.ruleCount == 0);
    // ...and uncommented it says exactly the defaults, every control in both modes
    uncomment(g_text, g_text2, sizeof(g_text2));
    int n = xrBindParse(&g_set, g_text2, &errs);
    CHECK(n == 0);
    if (n > 0) {
        fprintf(stderr, "first error: %s\n", errs.errors[0].text);
    }
    CHECK(g_set.ruleCount == 2 * XR_BIND_SRC_COUNT + 1);
    xrBindResolve(&g_set, XR_BIND_MODE_POINTER, NULL, &got[0]);
    xrBindResolve(&g_set, XR_BIND_MODE_GAMEPAD, NULL, &got[1]);
    CHECK(tablesSameActions(&got[0], &want[0]));
    CHECK(tablesSameActions(&got[1], &want[1]));
    CHECK(got[0].from[XR_BIND_SRC_RIGHT_Y] == XR_BIND_FROM_MODE);
    // A short buffer is cut off and still a string
    char small[16];
    CHECK(xrBindDefaultText(small, sizeof(small)) == len);
    CHECK(strlen(small) == sizeof(small) - 1);

    // The sample out and back: the same rules, and the same text again
    CHECK(xrBindParse(&g_set, SAMPLE, NULL) == 0);
    len = xrBindSerialize(&g_set, g_text, sizeof(g_text));
    CHECK(len > 0 && len < (int)sizeof(g_text));
    CHECK(xrBindParse(&g_set2, g_text, &errs) == 0);
    CHECK(g_set2.ruleCount == g_set.ruleCount);
    CHECK(g_set2.appCount == g_set.appCount);
    for (int i = 0; i < g_set.appCount && i < g_set2.appCount; i++) {
        CHECK(strcmp(g_set.apps[i], g_set2.apps[i]) == 0);
    }
    // The order is [pointer], [gamepad], then the apps; each rule is found
    for (int i = 0; i < g_set.ruleCount; i++) {
        const XrBindRule* a = &g_set.rules[i];
        int found = 0;
        for (int j = 0; j < g_set2.ruleCount; j++) {
            const XrBindRule* b = &g_set2.rules[j];
            found |= a->app == b->app && a->mode == b->mode && a->sources == b->sources
                     && xrBindActionEqual(&a->action, &b->action);
        }
        CHECK(found);
    }
    static const char* const APPS[] = { NULL, "Time Crisis 2", "Other Game" };
    for (int a = 0; a < 3; a++) {
        for (int mode = 0; mode < XR_BIND_MODES; mode++) {
            xrBindResolve(&g_set, mode, APPS[a], &want[0]);
            xrBindResolve(&g_set2, mode, APPS[a], &got[0]);
            CHECK(tablesSameActions(&got[0], &want[0]));
        }
    }
    CHECK(xrBindSerialize(&g_set2, g_text2, sizeof(g_text2)) == len);
    CHECK(strcmp(g_text, g_text2) == 0);
}

static void testSourcesDown(void) {
    PadHand l = frameHand(), r = frameHand();
    r.trigger = 0.7f;
    l.stickY = 0.6f;
    r.stickX = -0.55f;
    uint64_t d = xrBindSourcesDown(&l, &r, 0);
    CHECK(d == (S(RIGHT_TRIGGER) | S(LEFT_STICK_UP) | S(RIGHT_STICK_LEFT)));
    // Eased off, they hold to the lower mark
    r.trigger = 0.4f;
    l.stickY = 0.35f;
    r.stickX = -0.31f;
    CHECK(xrBindSourcesDown(&l, &r, d) == d);
    CHECK(xrBindSourcesDown(&l, &r, 0) == 0);
    r.trigger = 0.3f;
    l.stickY = 0.25f;
    r.stickX = -0.2f;
    CHECK(xrBindSourcesDown(&l, &r, d) == 0);
    // Each button where it belongs
    l = frameHand();
    r = frameHand();
    l.menu = 1;
    l.lower = 1;
    r.start = 1;
    r.faceY = 1;
    l.grip = 0.9f;
    CHECK(xrBindSourcesDown(&l, &r, 0)
          == (S(LEFT_VIEW) | S(LEFT_DPAD_DOWN) | S(RIGHT_MENU) | S(RIGHT_Y) | S(LEFT_GRIP)));
}

static void testFiles(void) {
    char tmpl[] = "/tmp/xrbindXXXXXX";
    char* dir = mkdtemp(tmpl);
    CHECK(dir != NULL);
    if (dir == NULL) {
        return;
    }
    char path[512];
    snprintf(path, sizeof(path), "%s/sub/dir/bindings.ini", dir);
    XrBindErrors errs;
    // The first run writes the commented default file, folders and all
    CHECK(xrBindLoadFile(&g_set, path, &errs) == 1);
    CHECK(g_set.ruleCount == 0);
    struct stat st;
    CHECK(stat(path, &st) == 0);
    CHECK(st.st_size == xrBindDefaultText(NULL, 0));
    CHECK(xrBindLoadFile(&g_set, path, &errs) == 0);
    CHECK(errs.count == 0);
    CHECK(g_set.ruleCount == 0);
    // An edited file is read, errors and all
    FILE* f = fopen(path, "wb");
    CHECK(f != NULL);
    if (f != NULL) {
        fputs("[pointer]\nright.x = key:F\nright.y = key:nope\n", f);
        fclose(f);
    }
    CHECK(xrBindLoadFile(&g_set, path, &errs) == 0);
    CHECK(g_set.ruleCount == 1);
    CHECK(errs.count == 1);
    // A folder in the file's place cannot be read
    char bad[512];
    snprintf(bad, sizeof(bad), "%s/sub", dir);
    CHECK(xrBindLoadFile(&g_set, bad, &errs) == -1);
    CHECK(errs.count == 1);
    CHECK(g_set.ruleCount == 0);

    // The default path follows MLXR_CONFIG_DIR, then HOME
    char buf[512];
    setenv("MLXR_CONFIG_DIR", dir, 1);
    snprintf(path, sizeof(path), "%s/bindings.ini", dir);
    CHECK(strcmp(xrBindDefaultPath(buf, sizeof(buf)), path) == 0);
    CHECK(xrBindLoadFile(&g_set, NULL, &errs) == 1);
    CHECK(stat(path, &st) == 0);
    unsetenv("MLXR_CONFIG_DIR");
    setenv("HOME", "/home/someone", 1);
    CHECK(strcmp(xrBindDefaultPath(buf, sizeof(buf)), "/home/someone/.config/moonlight-xr/bindings.ini")
          == 0);

    snprintf(path, sizeof(path), "rm -rf '%s'", dir);
    CHECK(system(path) == 0);
}

// docs/dev/bindings.md shows the default file; it has to be the one written
static void testDocMatches(void) {
    const char* path = MLXR_ASSETS_DIR "/../docs/dev/bindings.md";
    FILE* f = fopen(path, "rb");
    CHECK(f != NULL);
    if (f == NULL) {
        return;
    }
    size_t n = fread(g_text2, 1, sizeof(g_text2) - 1, f);
    fclose(f);
    g_text2[n] = '\0';
    const char* mark = strstr(g_text2, "<!-- bindings.ini default: begin -->");
    const char* start = mark != NULL ? strstr(mark, "```ini\n") : NULL;
    const char* end = start != NULL ? strstr(start + 7, "\n```\n") : NULL;
    CHECK(end != NULL);
    if (end == NULL) {
        return;
    }
    start += 7;
    size_t len = (size_t)(end - start) + 1;
    int want = xrBindDefaultText(g_text, sizeof(g_text));
    CHECK((size_t)want == len);
    CHECK(strncmp(g_text, start, len) == 0);
    for (size_t i = 0; i < len && i < (size_t)want; i++) {
        if (g_text[i] != start[i]) {
            fprintf(stderr, "bindings.md differs at %zu: \"%.40s\" against \"%.40s\"\n", i,
                    g_text + i, start + i);
            break;
        }
    }
}

int main(void) {
    testDocMatches();
    testNames();
    testMouseMap();
    testPointerDefaults();
    testGamepadDefaults();
    testDisconnect();
    testParseSample();
    testErrors();
    testRoundTrip();
    testSourcesDown();
    testFiles();
    return checksDone("bind");
}
