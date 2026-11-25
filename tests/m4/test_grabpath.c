// Moonlight XR Native, tests/m4: holds of the picture's bar through the whole input code (xrUpdateInput,
// the controllers read through the seams as in test_layergate) and xrBuildLayers, at 144 Hz. The trigger
// and the grip on the bar in the Gun and the other models, what the PC hears of it, how a hold ends, and
// where the beam that is drawn points. GPLv3.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "check.h"
#include "scene.h"

#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

XrResult XRAPI_CALL xrSyncActions(XrSession session, const XrActionsSyncInfo* info) {
    (void)session; (void)info;
    return XR_SUCCESS;
}

#define FRAME_NS 6944444LL
#define DEG 0.0174532925f
#define FAKE(n) ((uintptr_t)(n))
#define RIGHT_AIM ((XrSpace)FAKE(0x202))
#define LEFT_AIM ((XrSpace)FAKE(0x201))
#define VIEW ((XrSpace)FAKE(0x203))
#define LOCAL ((XrSpace)FAKE(0x204))

typedef struct { int live; XrPosef pose; float trigger, grip; XrVector2f stick; } Hand;
static Hand hand[2];
static XrPosef head;
static int64_t nowT = 1000000000LL;

// The session's clock is the test's: a frame is 1/144 s however long it takes here
int clock_gettime(clockid_t id, struct timespec* ts) {
    if (id != CLOCK_MONOTONIC) {
        return (int)syscall(SYS_clock_gettime, id, ts);
    }
    ts->tv_sec = (time_t)(nowT / 1000000000LL);
    ts->tv_nsec = (long)(nowT % 1000000000LL);
    return 0;
}
static float out[IN_SLOTS];

static XrResult fakeLocate(XrCtx* ctx, XrSpace space, XrSpace base, XrTime time, XrSpaceLocation* o) {
    (void)ctx; (void)base; (void)time;
    int h = space == LEFT_AIM ? 0 : (space == RIGHT_AIM ? 1 : -1);
    if (space == VIEW) {
        o->locationFlags = 0xf;
        o->pose = head;
        return XR_SUCCESS;
    }
    if (h < 0 || !hand[h].live) {
        return XR_ERROR_RUNTIME_FAILURE;
    }
    o->locationFlags = 0xf;
    o->pose = hand[h].pose;
    return XR_SUCCESS;
}
static int fakePoseActive(XrCtx* ctx, XrAction a, int h) {
    (void)ctx; (void)a;
    return h >= 0 && h < 2 && hand[h].live;
}
static float fakeFloat(XrCtx* ctx, XrAction a, int h) {
    if (h < 0 || h > 1) return 0.0f;
    return a == ctx->triggerAction ? hand[h].trigger : (a == ctx->grabAction ? hand[h].grip : 0.0f);
}
static int fakeBool(XrCtx* ctx, XrAction a, int h) { (void)ctx; (void)a; (void)h; return 0; }
static XrVector2f fakeVec2(XrCtx* ctx, XrAction a, int h) {
    XrVector2f z = { 0, 0 };
    return h >= 0 && h < 2 && a == ctx->scrollAction ? hand[h].stick : z;
}

static XrPosef aimAt(Vec3 from, Vec3 target) {
    Vec3 d = vecNorm(vecSub(target, from));
    Vec3 z = { -d.x, -d.y, -d.z };
    Vec3 up = { 0, 1, 0 };
    Vec3 x = vecNorm(vecCross(up, z));
    Vec3 y = vecCross(z, x);
    XrPosef p = { quatFromBasis(x, y, z), { from.x, from.y, from.z } };
    return p;
}

static const Vec3 RIGHT = { 0.25f, -0.35f, -0.3f };
static const Vec3 LEFT = { -0.25f, -0.4f, -0.25f };

// The picture width m wide, its middle at height y and away metres ahead, the right hand in front of it and
// the left one pointing up and out, as a hand that rests does
static void setUp(Scene* s, float width, float away, float y) {
    XrCtx* c = s->ctx;
    scene_reset(s);
    float place[10] = { 0.0f, y, -away, 0.0f, 0.0f, 0.0f, 1.0f, width, 2.5f, -1.0f };
    xrSetScreenPose(c, place);
    c->viewSpace = VIEW;
    c->localSpace = LOCAL;
    c->aimSpaces[0] = LEFT_AIM;
    c->aimSpaces[1] = RIGHT_AIM;
    c->aimAction = (XrAction)FAKE(0x401);
    c->triggerAction = (XrAction)FAKE(0x402);
    c->grabAction = (XrAction)FAKE(0x403);
    c->scrollAction = (XrAction)FAKE(0x404);
    c->rightClickAction = (XrAction)FAKE(0x405);
    c->middleClickAction = (XrAction)FAKE(0x406);
    c->toggleAction = (XrAction)FAKE(0x407);
    c->jointTracking = 0;
    c->locateSpace = fakeLocate;
    c->poseActive = fakePoseActive;
    c->readFloat = fakeFloat;
    c->readBool = fakeBool;
    c->readVec2 = fakeVec2;
    c->pointerSleepOn = 0;
    c->padMode = 0;
    c->eyeGaze = 0;
    c->profileKind[0] = c->profileKind[1] = PROFILE_CONTROLLER;
    c->headPos = (XrVector3f){ 0, 0, 0 };
    c->inputReady = 1;
    c->placementValid = 1;
    c->pointerOn = 1;
    c->splash.phase = SPLASH_GONE;
    c->screenTurnSet = 0;
    memset(hand, 0, sizeof(hand));
    memset(&head, 0, sizeof(head));
    head.orientation.w = 1.0f;
    hand[0].live = hand[1].live = 1;
    Vec3 up = { -0.6f, 2.0f, -0.6f }, mid = { 0.0f, y, -away };
    hand[0].pose = aimAt(LEFT, up);
    hand[1].pose = aimAt(RIGHT, mid);
}

static void useGun(XrCtx* c, int gun, int reload) {
    c->modelOn = 1;
    c->modelKind = gun ? CONTROLLER_MODEL_GUN : CONTROLLER_MODEL_FRAME;
    xrSetLightgun(c, LIGHTGUN_AUTO, reload, 1);
}

static Vec3 barPoint(XrCtx* c) {
    BarFrame f = barFrame(c);
    Vec3 local = { 0.0f, -(f.height * 0.5f + f.width * BAR_DROP_FRAC), 0.0f };
    float yaw = 0.0f;
    curveLocal(&local, f.radius, f.curved, &yaw);
    Vec3 r = quatRotate(f.pose.orientation, local);
    Vec3 p = { f.pose.position.x + r.x, f.pose.position.y + r.y, f.pose.position.z + r.z };
    return p;
}

static Vec3 kbBarPoint(XrCtx* c) {
    float w, h, drop;
    kbBarGeometry(c, &w, &h, &drop);
    Vec3 local = { 0.0f, -drop, 0.0f };
    Vec3 r = quatRotate(c->kbPose.orientation, local);
    Vec3 p = { c->kbPose.position.x + r.x, c->kbPose.position.y + r.y, c->kbPose.position.z + r.z };
    return p;
}

static Vec3 slotPoint(XrCtx* c, int slot) {
    BarFrame f = barFrame(c);
    Vec3 local;
    float side;
    barSlotPlacement(slot, f.width, f.height, &local, &side);
    Vec3 r = quatRotate(f.pose.orientation, local);
    Vec3 p = { f.pose.position.x + r.x, f.pose.position.y + r.y, f.pose.position.z + r.z };
    return p;
}

static Vec3 picturePoint(XrCtx* c, float u, float v) {
    float h = c->screenWidth * (float)c->videoHeight / (float)c->videoWidth;
    Vec3 p = { c->screenPose.position.x + (u - 0.5f) * c->screenWidth, c->screenPose.position.y + (0.5f - v) * h,
               c->screenPose.position.z };
    return p;
}

static Vec3 cornerPoint(XrCtx* c) {
    float side = cornerSide(c), high = c->screenWidth * (float)c->videoHeight / (float)c->videoWidth;
    return picturePoint(c, 1.0f + side * 0.5f / c->screenWidth, -side * 0.5f / high);
}

// ---- the beam as it is drawn ----
typedef struct { int frames; float worstOff, steepest, offAtSteepest; int strays; } Beam;
static Beam beam;

static float degOff(Vec3 a, Vec3 b) {
    float d = vecDot(vecNorm(a), vecNorm(b));
    return acosf(d > 1.0f ? 1.0f : d < -1.0f ? -1.0f : d) / DEG;
}

// The beam's quad in the list: it must lie along the line from a hand to the beam's end, whatever the head
// and the hold do, and its turn off its hand's aim is kept
static void look(Scene* s) {
    XrCtx* c = s->ctx;
    UiRectPx b = xrAtlasRect(ATLAS_BEAM);
    for (uint32_t i = 0; i < s->out.count; i++) {
        const XrCompositionLayerQuad* q = (const XrCompositionLayerQuad*)s->out.order[i];
        if (q->type != XR_TYPE_COMPOSITION_LAYER_QUAD || q->subImage.imageRect.offset.x != b.x
                || q->subImage.imageRect.offset.y != b.y || q->subImage.imageRect.extent.width != b.w) {
            continue;
        }
        Vec3 y = { 0, 1, 0 }, f = { 0, 0, -1 };
        Vec3 up = quatRotate(q->pose.orientation, y);
        Vec3 start = { c->beamStart.x, c->beamStart.y, c->beamStart.z };
        Vec3 end = { c->beamEnd.x, c->beamEnd.y, c->beamEnd.z };
        Vec3 along = vecSub(end, start);
        if (degOff(up, along) > 0.5f) {
            beam.strays++;
        }
        int h = c->beamHand;
        if (h < 0 || h > 1) {
            beam.strays++;
            continue;
        }
        Vec3 from = { hand[h].pose.position.x, hand[h].pose.position.y, hand[h].pose.position.z };
        Vec3 d = vecSub(start, from);
        if (vecDot(d, d) > 1e-6f) {
            beam.strays++;
        }
        float off = degOff(along, quatRotate(hand[h].pose.orientation, f));
        float steep = fabsf(asinf(vecNorm(along).y)) / DEG;
        beam.frames++;
        if (off > beam.worstOff) beam.worstOff = off;
        if (steep > beam.steepest) {
            beam.steepest = steep;
            beam.offAtSteepest = off;
        }
    }
}

static void step(Scene* s) {
    XrCtx* c = s->ctx;
    nowT += FRAME_NS;
    c->predictedDisplayTime = (XrTime)nowT;
    float distance, quadWidth, curvature;
    xrDefaultPlacement(&distance, &quadWidth, &curvature);
    xrUpdateInput(c, distance, quadWidth, curvature, 0, 1, 0, 0, out);
    scene_frame(s, NULL);
    look(s);
}
static void steps(Scene* s, int n) {
    for (int i = 0; i < n; i++) step(s);
}
static void trigger(int on) { hand[1].trigger = on ? 1.0f : 0.0f; }
static void grip(int on) { hand[1].grip = on ? 1.0f : 0.0f; }

// ---- the trigger on the bar ----
static void triggerHolds(Scene* s) {
    XrCtx* c = s->ctx;
    for (int kind = 0; kind < 3; kind++) {   // the Frame's model, the Gun, the Gun with the reload
        setUp(s, 3.6f, 3.0f, 0.0f);
        useGun(c, kind > 0, kind == 2);
        hand[1].pose = aimAt(RIGHT, barPoint(c));
        steps(s, 20);
        CHECK(c->hoverKind == HOVER_BAR && c->lightgun.active == (kind > 0));
        XrPosef was = c->screenPose;
        long pulses = c->recoilPulses;
        // the press takes hold, and the PC hears none of it from its first frame to its last
        trigger(1);
        for (int i = 0; i < 40; i++) {
            step(s);
            CHECK(c->grabMode == GRAB_MOVE && c->grabByTrigger && c->bindMouse == 0);
            CHECK(out[IN_BUTTONS] == 0.0f && c->buttonsDown == 0);
        }
        // it carries the picture
        Vec3 to = barPoint(c);
        to.y += 0.6f;
        hand[1].pose = aimAt(RIGHT, to);
        steps(s, 40);
        CHECK(c->screenPose.position.y > was.position.y + 0.3f);
        // the ray over where the picture was, still held: nothing shoots
        hand[1].pose = aimAt(RIGHT, picturePoint(c, 0.5f, 0.5f));
        steps(s, 10);
        CHECK(c->grabMode == GRAB_MOVE && out[IN_BUTTONS] == 0.0f);
        trigger(0);
        steps(s, 3);
        CHECK(c->grabMode == GRAB_NONE && c->buttonsDown == 0 && c->recoilPulses == pulses);

        // tracking lost in the hold: it ends, and the trigger still held clicks nothing when the hand is back on
        // the picture; the next press does
        setUp(s, 3.6f, 3.0f, 0.0f);
        hand[1].pose = aimAt(RIGHT, barPoint(c));
        steps(s, 20);
        trigger(1);
        steps(s, 5);
        CHECK(c->grabMode == GRAB_MOVE);
        hand[1].live = 0;
        steps(s, 200);
        CHECK(c->grabMode == GRAB_NONE);
        hand[1].live = 1;
        hand[1].pose = aimAt(RIGHT, picturePoint(c, 0.5f, 0.5f));
        steps(s, 60);
        CHECK(c->grabMode == GRAB_NONE && out[IN_BUTTONS] == 0.0f);
        trigger(0);
        steps(s, 30);
        trigger(1);
        steps(s, 10);
        CHECK(out[IN_BUTTONS] == (float)VR_BUTTON_LEFT);
        // and that shot, slid onto the bar, stays a shot
        hand[1].pose = aimAt(RIGHT, barPoint(c));
        steps(s, 10);
        CHECK(c->grabMode == GRAB_NONE && out[IN_BUTTONS] == (float)VR_BUTTON_LEFT);
        trigger(0);
        steps(s, 10);
        CHECK(out[IN_BUTTONS] == 0.0f);

        // the dashboard opens in the hold (the session loses focus): it ends, as with the grip, and the
        // trigger still held when it closes shoots nothing
        for (int byGrip = 0; byGrip < 2; byGrip++) {
            setUp(s, 3.6f, 3.0f, 0.0f);
            hand[1].pose = aimAt(RIGHT, barPoint(c));
            steps(s, 20);
            byGrip ? grip(1) : trigger(1);
            steps(s, 5);
            CHECK(c->grabMode == GRAB_MOVE && c->grabByTrigger == !byGrip);
            XrSessionState focused = c->sessionState;
            c->sessionState = XR_SESSION_STATE_VISIBLE;
            steps(s, 10);
            CHECK(c->grabMode == GRAB_NONE);
            c->sessionState = focused;
            hand[1].pose = aimAt(RIGHT, picturePoint(c, 0.5f, 0.5f));
            steps(s, 10);
            CHECK(c->grabMode == GRAB_NONE && out[IN_BUTTONS] == 0.0f);
            trigger(0);
            grip(0);
            steps(s, 10);
        }

        // the bar's buttons click as ever: no hold, nothing to the PC, not even slid onto the bar beside them
        setUp(s, 3.6f, 3.0f, 0.0f);
        c->cogOpen = 0;
        hand[1].pose = aimAt(RIGHT, slotPoint(c, BAR_SLOT_COG));
        steps(s, 20);
        CHECK(c->hoverKind == HOVER_COGBUTTON);
        trigger(1);
        for (int i = 0; i < 20; i++) {
            step(s);
            CHECK(c->grabMode == GRAB_NONE && out[IN_BUTTONS] == 0.0f && c->buttonsDown == 0);
        }
        CHECK(c->cogOpen);
        c->cogOpen = 0;
        hand[1].pose = aimAt(RIGHT, barPoint(c));
        for (int i = 0; i < 40; i++) {
            step(s);
            CHECK(c->grabMode == GRAB_NONE && out[IN_BUTTONS] == 0.0f);
        }
        trigger(0);
        steps(s, 10);
        CHECK(c->grabMode == GRAB_NONE && out[IN_BUTTONS] == 0.0f);

        // in a room, which holds the picture still, the bar takes nothing from either button
        setUp(s, 3.6f, 3.0f, 0.0f);
        hand[1].pose = aimAt(RIGHT, barPoint(c));
        steps(s, 20);
        CHECK(c->hoverKind == HOVER_BAR);
        c->roomStyle = ROOM_STYLE_FIRST;
        trigger(1);
        steps(s, 5);
        CHECK(c->grabMode == GRAB_NONE);
        trigger(0);
        steps(s, 5);
        grip(1);
        steps(s, 5);
        CHECK(c->grabMode == GRAB_NONE);
        grip(0);
        steps(s, 5);
        c->roomStyle = 0;
        steps(s, 5);
    }
    useGun(c, 0, 0);
}

// ---- the trigger on the keyboard's bar ----
// The void, then a room (which holds the picture still, but not the keyboard), whatever is drawn in the hand
static void keyboardBar(Scene* s) {
    XrCtx* c = s->ctx;
    for (int kind = 0; kind < 6; kind++) {   // the Frame's model, the Gun, the Gun with the reload; room: again
        int gun = kind % 3 > 0, room = kind >= 3;
        setUp(s, 3.6f, 3.0f, 0.0f);
        useGun(c, gun, kind % 3 == 2);
        c->roomStyle = room ? ROOM_STYLE_FIRST : 0;
        c->kbOpen = 1;
        c->kbMoved = 0;
        steps(s, 5);
        kbPlace(c);
        hand[1].pose = aimAt(RIGHT, kbBarPoint(c));
        steps(s, 20);
        CHECK(c->hoverKind == HOVER_KBBAR && c->lightgun.active == gun);
        XrPosef was = c->kbPose, picture = c->screenPose;
        long pulses = c->recoilPulses;
        trigger(1);
        for (int i = 0; i < 40; i++) {
            step(s);
            CHECK(c->grabMode == GRAB_KBMOVE && c->grabByTrigger && c->bindMouse == 0);
            CHECK(out[IN_BUTTONS] == 0.0f && c->buttonsDown == 0);
        }
        // it carries the keyboard and leaves the picture, and over the picture nothing shoots
        Vec3 to = kbBarPoint(c);
        to.y += 0.5f;
        hand[1].pose = aimAt(RIGHT, to);
        steps(s, 40);
        CHECK(c->kbPose.position.y > was.position.y + 0.2f);
        CHECK(memcmp(&c->screenPose, &picture, sizeof(picture)) == 0);
        hand[1].pose = aimAt(RIGHT, picturePoint(c, 0.5f, 0.5f));
        steps(s, 20);
        CHECK(c->grabMode == GRAB_KBMOVE && out[IN_BUTTONS] == 0.0f);
        trigger(0);
        steps(s, 5);
        CHECK(c->grabMode == GRAB_NONE && c->buttonsDown == 0 && c->recoilPulses == pulses);
        // the grip still takes it
        hand[1].pose = aimAt(RIGHT, kbBarPoint(c));
        steps(s, 40);
        CHECK(c->hoverKind == HOVER_KBBAR);
        grip(1);
        steps(s, 5);
        CHECK(c->grabMode == GRAB_KBMOVE && !c->grabByTrigger);
        grip(0);
        steps(s, 5);
        CHECK(c->grabMode == GRAB_NONE);
        if (room) {
            c->kbOpen = 0;
            c->roomStyle = 0;
            steps(s, 5);
            continue;
        }

        // a press started on the picture and slid onto the bar stays the PC's
        resetKeyboardPlacement(c);
        kbPlace(c);
        hand[1].pose = aimAt(RIGHT, picturePoint(c, 0.5f, 0.5f));
        steps(s, 60);
        CHECK(c->hoverKind == HOVER_SCREEN);
        trigger(1);
        steps(s, 10);
        CHECK(out[IN_BUTTONS] == (float)VR_BUTTON_LEFT);
        hand[1].pose = aimAt(RIGHT, kbBarPoint(c));
        for (int i = 0; i < 40; i++) {
            step(s);
            CHECK(c->grabMode == GRAB_NONE && out[IN_BUTTONS] == (float)VR_BUTTON_LEFT);
        }
        trigger(0);
        steps(s, 10);
        CHECK(out[IN_BUTTONS] == 0.0f);

        // the picture's corners: the trigger's without the Gun, the grip's alone with it
        c->kbOpen = 0;
        hand[1].pose = aimAt(RIGHT, cornerPoint(c));
        steps(s, 60);
        CHECK(c->hoverKind == HOVER_CORNER);
        trigger(1);
        steps(s, 5);
        CHECK(c->grabMode == (gun ? GRAB_NONE : GRAB_RESIZE));
        trigger(0);
        steps(s, 10);
        hand[1].pose = aimAt(RIGHT, cornerPoint(c));   // (the hold may have resized it)
        steps(s, 60);
        CHECK(c->hoverKind == HOVER_CORNER);
        grip(1);
        steps(s, 5);
        CHECK(c->grabMode == GRAB_RESIZE);
        grip(0);
        steps(s, 5);
    }
    useGun(c, 0, 0);
}

// ---- the beam round a hold ----
// (Reported once, 2026-11-25: a ray that went up and down, vertically, while the bar was held. The holds in that
// session's log, played here, draw none; what does is a picture pulled into the face, whose bar is far below it.)
static void hold(int on) { grip(on); }

static Beam played(Scene* s, const char* what, float width, float from, float y, float stickY, int frames, int roll, float headYaw,
                   int other) {
    XrCtx* c = s->ctx;
    setUp(s, width, from, y);
    hand[1].pose = aimAt(RIGHT, barPoint(c));
    steps(s, 30);
    CHECK(c->hoverKind == HOVER_BAR);
    memset(&beam, 0, sizeof(beam));
    c->beamWatchLines = 0;
    XrPosef aim = hand[1].pose;
    hold(1);
    steps(s, 2);
    CHECK(c->grabMode == GRAB_MOVE && c->beamHand == 1);
    hand[1].stick.y = stickY;
    Vec3 fwd = { 0, 0, -1 }, up = { 0, 1, 0 };
    for (int i = 0; i < frames; i++) {
        float t = (float)i / (float)frames;
        hand[1].pose.orientation = quatMul(aim.orientation, axisAngleQuat(fwd, (float)roll * DEG * t));
        head.orientation = axisAngleQuat(up, headYaw * DEG * t);
        if (other) {
            hand[0].trigger = (i / 20) % 2 ? 1.0f : 0.0f;
        }
        step(s);
        CHECK(c->grabMode == GRAB_MOVE && c->beamHand == 1);
    }
    hand[1].stick.y = 0.0f;
    hold(0);
    steps(s, 60);
    CHECK(c->grabMode == GRAB_NONE);
    hand[0].trigger = 0.0f;
    printf("  %-34s beam in %d frames, up to %.0f deg off the hand's aim, %.0f deg steep (%.0f off there), %d lines; ends %.2f m off\n",
           what, beam.frames, beam.worstOff, beam.steepest, beam.offAtSteepest, c->beamWatchLines,
           sqrtf(c->screenPose.position.x * c->screenPose.position.x + c->screenPose.position.z * c->screenPose.position.z));
    CHECK(beam.strays == 0);
    head.orientation = (XrQuaternionf){ 0, 0, 0, 1 };
    return beam;
}

static void beams(Scene* s) {
    useGun(s->ctx, 1, 0);
    Beam b = played(s, "logged: 5.0 m, pushed 2.0 m", 5.95f, 5.0f, -0.3f, 1.0f, 200, 0, 0, 0);
    CHECK(b.worstOff < 15.0f && b.steepest < 45.0f);
    b = played(s, "logged: 7.0 m, held", 5.95f, 7.0f, -0.3f, 0.0f, 200, 0, 0, 0);
    CHECK(b.worstOff < 15.0f && b.steepest < 45.0f);
    b = played(s, "logged: 7.1 m, pulled 2.7 m", 5.95f, 7.06f, -0.3f, -1.0f, 260, 0, 0, 0);
    CHECK(b.worstOff < 15.0f && b.steepest < 45.0f);
    b = played(s, "the other hand up and pressing", 5.95f, 5.0f, -0.3f, 0.0f, 300, 0, 0, 1);
    CHECK(b.worstOff < 15.0f && b.steepest < 45.0f);
    b = played(s, "the head turned 70 deg", 5.95f, 5.0f, -0.3f, 0.0f, 300, 0, 70.0f, 0);
    CHECK(b.steepest < 45.0f);
    played(s, "the wrist rolled 90 deg", 5.95f, 5.0f, -0.3f, 0.0f, 300, 90, 0, 0);
    // into the face: the bar of a picture 6 m wide is nearly 2 m under it, and that is where the beam goes
    b = played(s, "pulled into the face", 5.95f, 5.0f, -0.3f, -1.0f, 500, 0, 0, 0);
    CHECK(b.steepest > 60.0f && s->ctx->beamWatchLines == 1);
    useGun(s->ctx, 0, 0);
}

int main(void) {
    setenv("XR_LOADER_DEBUG", "none", 1);
    Scene* s = scene_new();
    triggerHolds(s);
    keyboardBar(s);
    beams(s);
    scene_free(s);
    return checksDone("m4 grabpath");
}
