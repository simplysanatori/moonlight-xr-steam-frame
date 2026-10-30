// Moonlight XR Native, tests/m4: bindings.ini wired into the input pass (lane
// wave2-bind-wire). The file is read into a context (written with the
// defaults the first time), and the bindings' part of a frame (xrBindFrame,
// as xrUpdateInput runs it) is fed controls: the defaults click as they always
// did and give the idle buttons theirs, a sample file changes the host button
// the trigger sends and the app command Y fires, keys go down and up once,
// a mode switch lets go, an app's own lines and gamepad mode's table apply,
// and a file with mistakes says so on the toast. GPLv3.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "check.h"
#include "scene.h"
#include "xr_depth.h"

static char g_dir[64];
static char g_path[128];

static void writeFile(const char* text) {
    FILE* f = fopen(g_path, "wb");
    CHECK(f != NULL);
    if (f != NULL) {
        fputs(text, f);
        fclose(f);
    }
}

// The controls at rest, both controllers Frame controllers in hand
static void rest(XrCtx* c) {
    for (int h = 0; h < HAND_COUNT; h++) {
        memset(&c->padSeen[h], 0, sizeof(c->padSeen[h]));
        c->padSeen[h].frame = 1;
        c->profileKind[h] = PROFILE_CONTROLLER;
        c->triggerDown[h] = 0;
        c->triggerSwallowed[h] = 0;
    }
}

static float g_out[IN_SLOTS];

static void frame(XrCtx* c) {
    memset(g_out, 0, sizeof(g_out));
    g_out[IN_SETTING] = -1.0f;
    PadState s;
    padRest(&s);
    xrBindFrame(c, 1000000000LL, 1.0f / 90.0f, &s, g_out);
}

static void defaultsFile(void) {
    Scene* sc = scene_new();
    XrCtx* c = sc->ctx;
    unlink(g_path);
    // No file: the defaults are written, every line commented out
    CHECK(xrBindLoad(c, g_path) == 0);
    CHECK(access(g_path, R_OK) == 0);
    CHECK(c->bindPadCustom == 0 && c->bindGuideHold == 1);
    // Read back, it is the defaults
    CHECK(xrBindLoad(c, g_path) == 0);
    rest(c);

    // The right trigger is the left button (Moonlight's BUTTON_LEFT, 1)
    c->triggerDown[HAND_RIGHT] = 1;
    frame(c);
    CHECK(c->bindMouse == VR_BUTTON_LEFT);
    CHECK(xrBindHostButton(0) == 1);
    // ...not while the press was spent on our UI
    c->triggerSwallowed[HAND_RIGHT] = 1;
    frame(c);
    CHECK(c->bindMouse == 0);
    rest(c);
    // A, B, the d-pad's down and right and the stick clicks as before
    c->padSeen[HAND_RIGHT].lower = 1;
    frame(c);
    CHECK(c->bindMouse == VR_BUTTON_RIGHT);
    rest(c);
    c->padSeen[HAND_LEFT].upper = 1;
    frame(c);
    CHECK(c->bindMouse == VR_BUTTON_MIDDLE);
    rest(c);
    c->padSeen[HAND_LEFT].stickClick = 1;
    frame(c);
    CHECK(c->bindMouse == (c->stickRightClick ? VR_BUTTON_RIGHT : 0));
    rest(c);
    // A hand pinching is the left button whatever is bound
    c->profileKind[HAND_LEFT] = PROFILE_HANDS;
    c->triggerDown[HAND_LEFT] = 1;
    frame(c);
    CHECK(c->bindMouse == VR_BUTTON_LEFT);
    rest(c);
    // The bumpers are back and forward, Moonlight's X1 and X2
    c->padSeen[HAND_LEFT].bumper = 1;
    c->padSeen[HAND_RIGHT].bumper = 1;
    frame(c);
    CHECK(c->bindMouse == (8 | 16));
    CHECK(xrBindHostButton(3) == 4 && xrBindHostButton(4) == 5);
    rest(c);
    frame(c);
    CHECK(c->bindMouse == 0);

    // Y shows the keyboard, and again hides it
    CHECK(c->kbOpen == 0);
    c->padSeen[HAND_RIGHT].faceY = 1;
    frame(c);
    CHECK(c->kbOpen == 1);
    frame(c);
    CHECK(c->kbOpen == 1);
    rest(c);
    frame(c);
    c->padSeen[HAND_RIGHT].faceY = 1;
    frame(c);
    CHECK(c->kbOpen == 0);
    rest(c);
    frame(c);
    // Menu opens the cog
    c->padSeen[HAND_RIGHT].start = 1;
    frame(c);
    CHECK(c->cogOpen == 1);
    rest(c);
    frame(c);
    c->cogOpen = 0;
    // Nothing went to the keys
    CHECK(c->bindKeyCount == 0);
    scene_free(sc);
}

static const char* SAMPLE =
    "[pointer]\n"
    "right.trigger = mouse:right\n"
    "right.y = app:ray\n"
    "right.x = key:ctrl+F\n"
    "right.a+right.b = wheel:up\n"
    "left.stick = stick:arrows\n"
    "[app \"Time Crisis 2\"]\n"
    "pointer.right.trigger = mouse:middle\n"
    "[gamepad]\n"
    "right.a = pad:b\n"
    "right.grip = key:R\n";

static void sampleFile(void) {
    Scene* sc = scene_new();
    XrCtx* c = sc->ctx;
    writeFile(SAMPLE);
    CHECK(xrBindLoad(c, g_path) == 0);
    CHECK(c->bindPadCustom == 1 && c->bindGuideHold == 1);
    rest(c);

    // The right trigger sends the right button now: Moonlight's BUTTON_RIGHT, 3
    c->triggerDown[HAND_RIGHT] = 1;
    frame(c);
    CHECK(c->bindMouse == VR_BUTTON_RIGHT);
    CHECK(xrBindHostButton(1) == 3);
    rest(c);
    frame(c);

    // Y steps the ray: Ray, then Off, saved as the Display tab's row is
    CHECK(cogOptionValue(c, COG_OPTION_RAY, 0) == 2);
    c->padSeen[HAND_RIGHT].faceY = 1;
    frame(c);
    CHECK(cogOptionValue(c, COG_OPTION_RAY, 0) == 0);
    CHECK((int)g_out[IN_SETTING] == SETTING_POINTER_DOT && (int)g_out[IN_SETTING_VALUE] == 0);
    CHECK(c->kbOpen == 0);
    rest(c);
    frame(c);
    c->padSeen[HAND_RIGHT].faceY = 1;
    frame(c);
    CHECK(cogOptionValue(c, COG_OPTION_RAY, 0) == 1);
    rest(c);
    frame(c);

    // X is Ctrl+F, down once and up once
    c->bindKeyCount = 0;
    c->padSeen[HAND_RIGHT].faceX = 1;
    frame(c);
    frame(c);
    CHECK(c->bindKeyCount == 1);
    CHECK(c->bindKeys[0].vk == 'F' && c->bindKeys[0].mods == XR_BIND_MOD_CTRL && c->bindKeys[0].down);
    rest(c);
    frame(c);
    CHECK(c->bindKeyCount == 2 && !c->bindKeys[1].down && c->bindKeys[1].vk == 'F');
    c->bindKeyCount = 0;

    // A with B is a notch up, and neither clicks
    c->padSeen[HAND_RIGHT].lower = 1;
    c->padSeen[HAND_RIGHT].upper = 1;
    frame(c);
    CHECK(c->bindMouse == 0 && c->bindWheelV == SCROLL_HIRES_NOTCH);
    c->bindWheelV = 0;
    rest(c);
    frame(c);

    // The left stick is the arrow keys
    c->padSeen[HAND_LEFT].stickX = -0.9f;
    frame(c);
    CHECK(c->bindKeyCount == 1 && c->bindKeys[0].vk == 0x25 && c->bindKeys[0].down);
    rest(c);
    frame(c);
    c->bindKeyCount = 0;

    // The app's own line
    xrBindSetApp(c, "time crisis 2");
    c->triggerDown[HAND_RIGHT] = 1;
    frame(c);
    CHECK(c->bindMouse == VR_BUTTON_MIDDLE);
    CHECK(xrBindHostButton(2) == 2);
    xrBindSetApp(c, "");
    rest(c);
    frame(c);
    c->triggerDown[HAND_RIGHT] = 1;
    frame(c);
    CHECK(c->bindMouse == VR_BUTTON_RIGHT);
    rest(c);
    frame(c);

    // A key held across the switch to gamepad mode is let go, and the
    // controls still down stay out of the pad until let go
    c->bindKeyCount = 0;
    c->padSeen[HAND_RIGHT].faceX = 1;
    frame(c);
    CHECK(c->bindKeyCount == 1);
    c->padMode = 1;
    c->padSeen[HAND_RIGHT].lower = 1;
    memset(g_out, 0, sizeof(g_out));
    PadState s;
    padRest(&s);
    xrBindFrame(c, 2000000000LL, 1.0f / 90.0f, &s, g_out);
    CHECK(c->bindKeyCount == 2 && !c->bindKeys[1].down);
    CHECK(s.buttons == 0);
    rest(c);
    padRest(&s);
    xrBindFrame(c, 2011111111LL, 1.0f / 90.0f, &s, g_out);

    // Gamepad mode's table: A is B, the right grip is R and not RB, the left
    // grip at its default is pad_grip's LB
    c->bindKeyCount = 0;
    c->padSeen[HAND_RIGHT].lower = 1;
    c->padSeen[HAND_RIGHT].grip = 1.0f;
    c->padGrip[HAND_LEFT] = 1;
    c->padGripMode = PAD_GRIP_BUMPERS;
    padRest(&s);
    xrBindFrame(c, 2022222222LL, 1.0f / 90.0f, &s, g_out);
    CHECK(s.buttons == (PAD_B | PAD_LB));
    CHECK(c->bindKeyCount == 1 && c->bindKeys[0].vk == 'R' && c->bindKeys[0].down);
    // The triggers stay analogue
    c->padSeen[HAND_LEFT].trigger = 0.5f;
    padRest(&s);
    xrBindFrame(c, 2033333333LL, 1.0f / 90.0f, &s, g_out);
    CHECK(s.leftTrigger == padTrigger(0.5f) && s.rightTrigger == 0);
    // The sticks are the pad's
    c->padSeen[HAND_RIGHT].stickX = 1.0f;
    padRest(&s);
    xrBindFrame(c, 2044444444LL, 1.0f / 90.0f, &s, g_out);
    CHECK(s.rightX == PAD_STICK_FULL && s.leftX == 0);
    c->padMode = 0;
    c->padGrip[HAND_LEFT] = 0;
    scene_free(sc);
}

// The defaults in gamepad mode leave padMapGrips's pad as it is
static void gamepadDefaults(void) {
    Scene* sc = scene_new();
    XrCtx* c = sc->ctx;
    unlink(g_path);
    CHECK(xrBindLoad(c, g_path) == 0);
    rest(c);
    c->padMode = 1;
    PadState s;
    padRest(&s);
    s.buttons = PAD_A | PAD_START;
    s.leftTrigger = 77;
    s.rightX = 1234;
    c->padSeen[HAND_RIGHT].lower = 1;
    memset(g_out, 0, sizeof(g_out));
    xrBindFrame(c, 1000000000LL, 1.0f / 90.0f, &s, g_out);
    CHECK(s.buttons == (PAD_A | PAD_START) && s.leftTrigger == 77 && s.rightX == 1234);
    CHECK(c->bindKeyCount == 0 && c->bindPadMouse == 0);
    scene_free(sc);
}

// A grip the file binds is the file's, pad:rb included: pad_grip no longer
// applies to it, while the other grip (left alone) still follows pad_grip
static void explicitGrip(void) {
    Scene* sc = scene_new();
    XrCtx* c = sc->ctx;
    writeFile("[gamepad]\nright.grip = pad:rb\n");
    CHECK(xrBindLoad(c, g_path) == 0);
    CHECK(c->bindPadCustom == 1);
    CHECK(c->bindTable[XR_BIND_MODE_GAMEPAD].from[XR_BIND_SRC_RIGHT_GRIP] == XR_BIND_FROM_MODE);
    rest(c);
    c->padMode = 1;
    c->padGripMode = PAD_GRIP_NONE;
    c->padSeen[HAND_RIGHT].grip = 1.0f;
    c->padSeen[HAND_LEFT].grip = 1.0f;
    c->padGrip[HAND_LEFT] = 1;
    c->padGrip[HAND_RIGHT] = 1;
    PadState s;
    padRest(&s);
    memset(g_out, 0, sizeof(g_out));
    xrBindFrame(c, 1000000000LL, 1.0f / 90.0f, &s, g_out);
    CHECK(s.buttons == PAD_RB);
    c->padGripMode = PAD_GRIP_STICK_CLICKS;
    padRest(&s);
    xrBindFrame(c, 1011111111LL, 1.0f / 90.0f, &s, g_out);
    CHECK(s.buttons == (PAD_RB | PAD_LS_CLICK));
    scene_free(sc);
}

// app:host opens and closes the Host sheet; app:bringhere asks the layout
// to bring the screen (it may decline without a head pose, but never fails)
static void hostAndBringHere(void) {
    Scene* sc = scene_new();
    XrCtx* c = sc->ctx;
    writeFile("[pointer]\nright.y = app:host\nleft.dpad_left = app:bringhere\n");
    CHECK(xrBindLoad(c, g_path) == 0);
    rest(c);
    CHECK(c->hostOpen == 0);
    c->padSeen[HAND_RIGHT].faceY = 1;
    frame(c);
    CHECK(c->hostOpen == 1);
    rest(c);
    frame(c);
    c->padSeen[HAND_RIGHT].faceY = 1;
    frame(c);
    CHECK(c->hostOpen == 0);
    rest(c);
    frame(c);
    c->padSeen[HAND_LEFT].dpadLeft = 1;
    frame(c);
    CHECK(c->hostOpen == 0 && c->kbOpen == 0);
    scene_free(sc);
}

// Mistakes in the file: each a warning in the log, and one toast for them all
static void fileWithErrors(void) {
    Scene* sc = scene_new();
    XrCtx* c = sc->ctx;
    writeFile("[pointer]\nright.y = app:nothing\nright.q = key:F\nright.x = key:G\n");
    c->notices.count = 0;
    CHECK(xrBindLoad(c, g_path) == 2);
    CHECK(c->notices.count == 1);
    // The good line still counts
    CHECK(c->bindTable[XR_BIND_MODE_POINTER].single[XR_BIND_SRC_RIGHT_X].kind == XR_BIND_ACT_KEY);
    // Notices off: nothing on the toast
    noticeSetLevel(&c->notices, NOTICES_OFF);
    c->notices.count = 0;
    CHECK(xrBindLoad(c, g_path) == 2);
    CHECK(c->notices.count == 0);
    scene_free(sc);
}

// PLAN 4b.6: app:zoomin, app:zoomout and app:zoomreset are in the table with the
// codes xr_input.c's bindAppCommand switches on, and what that switch does for
// each is xrZoomStep(ctx, +1 / -1 / 0), aimed at the pointer
static void zoomActions(void) {
    Scene* sc = scene_new();
    XrCtx* c = sc->ctx;
    writeFile("[pointer]\nright.y = app:zoomin\nright.x = app:zoomout\nleft.dpad_left = app:zoomreset\n");
    CHECK(xrBindLoad(c, g_path) == 0);
    const XrBindTable* t = &c->bindTable[XR_BIND_MODE_POINTER];
    CHECK(t->single[XR_BIND_SRC_RIGHT_Y].kind == XR_BIND_ACT_APP && t->single[XR_BIND_SRC_RIGHT_Y].code == XR_BIND_APP_ZOOMIN);
    CHECK(t->single[XR_BIND_SRC_RIGHT_X].kind == XR_BIND_ACT_APP && t->single[XR_BIND_SRC_RIGHT_X].code == XR_BIND_APP_ZOOMOUT);
    CHECK(t->single[XR_BIND_SRC_LEFT_DPAD_LEFT].kind == XR_BIND_ACT_APP
          && t->single[XR_BIND_SRC_LEFT_DPAD_LEFT].code == XR_BIND_APP_ZOOMRESET);
    // A file that names an unknown one says so on the toast and keeps the rest
    writeFile("[pointer]\nright.y = app:zoomin\nright.x = app:zoomed\n");
    c->notices.count = 0;
    CHECK(xrBindLoad(c, g_path) == 1);
    CHECK(c->notices.count == 1);
    CHECK(c->bindTable[XR_BIND_MODE_POINTER].single[XR_BIND_SRC_RIGHT_Y].code == XR_BIND_APP_ZOOMIN);

    // What each does, the way the owner's switch calls it, from the whole picture
    CHECK(c->zoomPct == 100);
    c->zoomHitValid = 1;
    c->zoomHitU = 0.9f;
    c->zoomHitV = 0.1f;
    CHECK(xrZoomStep(c, 1) == 1 && c->zoomPct == 125);
    CHECK(xrZoomStep(c, 1) == 1 && c->zoomPct == 150);
    CHECK(xrZoomStep(c, -1) == 1 && c->zoomPct == 125);
    CHECK(c->zoomX > 50 && c->zoomY < 50);   // toward the corner the pointer was on
    CHECK(xrZoomStep(c, 0) == 1 && c->zoomPct == 100 && c->zoomX == 50 && c->zoomY == 50);
    CHECK(xrZoomStep(c, 0) == 0);   // nothing to reset
    CHECK(xrZoomStep(c, -1) == 0);  // nothing to zoom out of
    for (int i = 0; i < 20; i++) xrZoomStep(c, 1);
    CHECK(c->zoomPct == 400 && xrZoomStep(c, 1) == 0);
    // The corner it was aimed at is on screen under the pointer
    float u = 0.9f, v = 0.1f;
    xrDepthPointerUv(c, &u, &v);
    CHECK_NEAR(u, 0.9, 0.01);
    CHECK_NEAR(v, 0.1, 0.01);
    // No pointer on the screen: about the middle of the view
    xrZoomStep(c, 0);
    c->zoomHitValid = 0;
    xrZoomStep(c, 1);
    CHECK(c->zoomPct == 125 && c->zoomX == 50 && c->zoomY == 50);
    scene_free(sc);
}

int main(void) {
    snprintf(g_dir, sizeof(g_dir), "/tmp/mlxr-bind-XXXXXX");
    if (mkdtemp(g_dir) == NULL) {
        perror("mkdtemp");
        return 1;
    }
    snprintf(g_path, sizeof(g_path), "%s/bindings.ini", g_dir);
    defaultsFile();
    sampleFile();
    gamepadDefaults();
    explicitGrip();
    hostAndBringHere();
    zoomActions();
    fileWithErrors();
    unlink(g_path);
    rmdir(g_dir);
    return checksDone("m4 bindings");
}
