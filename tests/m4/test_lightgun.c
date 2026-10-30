// Moonlight XR Native, tests/m4: the lightgun pointer policy and the gamepad's gyro as the input
// pass wires them (PLAN 2.3 and 2.9, lane wave2-lightgun-gyro). The decisions themselves are in
// tests/xrcore (test_lightgun, test_padgyro); this is the glue: the policy follows the model
// kind and the settings and holds pointer sleep off without changing the setting, the trigger
// grabs the screen's bar unless the policy is on, shots recoil and an off-picture pull is a
// right click; the gyro goes to the host only when the opt-in is on and the host asked, from the
// controller's aim pose or the head, through the ratchet. GPLv3.
#include <math.h>

#include "check.h"
#include "scene.h"
#include "xr_pad_bridge.h"

#define FAKE(n) ((uintptr_t)(n))
#define LEFT_AIM ((XrSpace)FAKE(0x201))
#define RIGHT_AIM ((XrSpace)FAKE(0x202))
#define VIEW ((XrSpace)FAKE(0x203))
#define LOCAL ((XrSpace)FAKE(0x204))
#define TOUCH_ACTION ((XrAction)FAKE(0x410))
#define DEG 57.2957795

static XrPosef aimAt(Vec3 from, Vec3 target) {
    Vec3 d = vecNorm(vecSub(target, from));
    Vec3 z = { -d.x, -d.y, -d.z };
    Vec3 up = { 0, 1, 0 };
    Vec3 x = vecNorm(vecCross(up, z));
    Vec3 y = vecCross(z, x);
    XrPosef p = { quatFromBasis(x, y, z), { from.x, from.y, from.z } };
    return p;
}

// The controller model drawn in the hands: the gun, or the Frame's
static void useGun(XrCtx* c, int gun) {
    c->modelOn = 1;
    c->modelKind = gun ? CONTROLLER_MODEL_GUN : CONTROLLER_MODEL_FRAME;
}

// ---- the pointer policy ---------------------------------------------------------------------------------

static void followsModel(Scene* s) {
    XrCtx* c = s->ctx;
    c->pointerSleepOn = 1;
    // auto with the Frame's model: nothing is changed
    useGun(c, 0);
    xrSetLightgun(c, LIGHTGUN_AUTO, 0, 1);
    CHECK(!c->lightgun.active && !c->lightgun.sleepOff && !c->lightgun.gripGrabOnly);
    // auto with the gun: the policy, and the stored pointer sleep is untouched (the cog still shows it)
    useGun(c, 1);
    xrSetLightgun(c, LIGHTGUN_AUTO, 0, 1);
    CHECK(c->lightgun.active && c->lightgun.sleepOff && c->lightgun.clicksRaw && c->lightgun.gripGrabOnly);
    CHECK(c->pointerSleepOn == 1);
    CHECK(!c->lightgun.reload && c->lightgun.recoil);
    // forced off with the gun in the hand, forced on with something else
    xrSetLightgun(c, LIGHTGUN_OFF, 1, 1);
    CHECK(!c->lightgun.active && !c->lightgun.reload);
    useGun(c, 0);
    xrSetLightgun(c, LIGHTGUN_ON, 1, 0);
    CHECK(c->lightgun.active && c->lightgun.reload && !c->lightgun.recoil);
    // the model switched off, the kind kept: no gun in the hand, no policy under auto
    xrSetLightgun(c, LIGHTGUN_AUTO, 1, 1);
    c->modelKind = CONTROLLER_MODEL_GUN;
    c->modelOn = 0;
    xrSetLightgun(c, LIGHTGUN_AUTO, 1, 1);
    CHECK(!c->lightgun.active);
    // out of range is auto
    c->modelOn = 1;
    xrSetLightgun(c, 77, 1, 1);
    CHECK(c->lightgunSetting == LIGHTGUN_AUTO && c->lightgun.active);
    // the policy tracks a change of model from the cog on the next frame, not only at the setting
    c->modelKind = CONTROLLER_MODEL_GENERIC;
    xrButtonsFrame(c, 0, HOVER_NONE, 0);
    CHECK(!c->lightgun.active);
    c->modelKind = CONTROLLER_MODEL_GUN;
    xrButtonsFrame(c, 0, HOVER_NONE, 0);
    CHECK(c->lightgun.active);
}

// ---- pointer sleep -----------------------------------------------------------------------------------------

static XrResult noPoses(XrCtx* ctx, XrSpace space, XrSpace base, XrTime time, XrSpaceLocation* out) {
    (void)ctx; (void)space; (void)base; (void)time; (void)out;
    return XR_ERROR_RUNTIME_FAILURE;
}
static int noPose(XrCtx* ctx, XrAction action, int hand) { (void)ctx; (void)action; (void)hand; return 0; }
static float noFloat(XrCtx* ctx, XrAction action, int hand) { (void)ctx; (void)action; (void)hand; return 0.0f; }
static int noBool(XrCtx* ctx, XrAction action, int hand) { (void)ctx; (void)action; (void)hand; return 0; }
static XrVector2f noVec2(XrCtx* ctx, XrAction action, int hand) {
    (void)ctx; (void)action; (void)hand;
    XrVector2f zero = { 0.0f, 0.0f };
    return zero;
}

// Two controllers lying still for twenty seconds: the pointer goes to sleep, unless the policy holds it up
static int stillFor20s(XrCtx* c) {
    c->locateSpace = noPoses;
    c->poseActive = noPose;
    c->readFloat = noFloat;
    c->readBool = noBool;
    c->readVec2 = noVec2;
    c->profileKind[0] = PROFILE_CONTROLLER;
    c->profileKind[1] = PROFILE_CONTROLLER;
    c->pointerAwake = 1;
    c->stillFor = 0.0f;
    c->lastInputNs = 0;
    int64_t t = 1000000000LL;
    for (int i = 0; i < 1800; i++) {
        t += 11111111LL;
        xrPointerStep(c, t, 0);
    }
    return c->pointerAwake;
}

static void pointerSleep(Scene* s) {
    XrCtx* c = s->ctx;
    scene_reset(s);
    c->pointerSleepOn = 1;
    useGun(c, 0);
    xrSetLightgun(c, LIGHTGUN_AUTO, 0, 1);
    CHECK(stillFor20s(c) == 0);      // the Frame's model: it sleeps, as ever
    useGun(c, 1);
    xrSetLightgun(c, LIGHTGUN_AUTO, 0, 1);
    CHECK(stillFor20s(c) == 1);      // the gun: it never does
    CHECK(c->pointerSleepOn == 1);   // and the setting is as the user left it
    xrSetLightgun(c, LIGHTGUN_OFF, 0, 1);
    CHECK(stillFor20s(c) == 0);      // forced off with the gun in the hand: it sleeps
    c->locateSpace = NULL;
    c->poseActive = NULL;
    c->readFloat = NULL;
    c->readBool = NULL;
    c->readVec2 = NULL;
    c->profileKind[0] = c->profileKind[1] = 0;
    c->pointerAwake = 1;
}

// ---- grabbing the screen ---------------------------------------------------------------------------------

static const Vec3 HAND = { 0.25f, -0.35f, -0.3f };

// The owner's picture, flat, 3 m out and 3.6 m wide, and the middle of its move bar
static void pictureAt3m(XrCtx* c) {
    c->headPos = (XrVector3f){ 0, 0, 0 };
    c->kbOpen = 0;
    c->panelCurve = 0.0f;
    memset(&c->screenPose, 0, sizeof(c->screenPose));
    c->screenPose.orientation.w = 1.0f;
    c->screenPose.position.z = -3.0f;
    c->screenWidth = 3.6f;
    c->screenRadius = 12.0f;
    c->grabMode = GRAB_NONE;
}

static Vec3 pictureBarPoint(XrCtx* c) {
    BarFrame f = barFrame(c);
    Vec3 local = { 0.0f, -(f.height * 0.5f + f.width * BAR_DROP_FRAC), 0.0f };
    float yaw = 0.0f;
    curveLocal(&local, f.radius, f.curved, &yaw);
    Vec3 r = quatRotate(f.pose.orientation, local);
    Vec3 p = { f.pose.position.x + r.x, f.pose.position.y + r.y, f.pose.position.z + r.z };
    return p;
}

static void gripOnlyGrab(Scene* s) {
    XrCtx* c = s->ctx;
    scene_reset(s);
    pictureAt3m(c);
    XrPosef aim = aimAt(HAND, pictureBarPoint(c));

    // without the policy the trigger on the bar's handle takes hold of the screen, as ever
    useGun(c, 0);
    xrSetLightgun(c, LIGHTGUN_AUTO, 0, 1);
    CHECK(xrGrabStepAt(c, aim, 0, 0, 0.0f) == HOVER_BAR);
    CHECK(c->grabMode == GRAB_NONE);
    xrGrabStepAt(c, aim, 1, 0, 0.0f);
    CHECK(c->grabMode == GRAB_MOVE && c->grabByTrigger);
    xrGrabStepAt(c, aim, 0, 0, 0.0f);
    CHECK(c->grabMode == GRAB_NONE);

    // with it the trigger shoots and the screen stays where it is
    useGun(c, 1);
    xrSetLightgun(c, LIGHTGUN_AUTO, 0, 1);
    XrPosef before = c->screenPose;
    xrGrabStepAt(c, aim, 1, 0, 0.0f);
    CHECK(c->grabMode == GRAB_NONE);
    xrGrabStepAt(c, aim, 1, 0, 0.0f);
    CHECK(c->grabMode == GRAB_NONE);
    xrGrabStepAt(c, aim, 0, 0, 0.0f);
    CHECK(c->screenPose.position.x == before.position.x && c->screenPose.position.y == before.position.y
          && c->screenPose.position.z == before.position.z);
    // the grip still does
    xrGrabStepAt(c, aim, 0, 1, 0.0f);
    CHECK(c->grabMode == GRAB_MOVE && !c->grabByTrigger);
    xrGrabStepAt(c, aim, 0, 0, 0.0f);
    CHECK(c->grabMode == GRAB_NONE);
    // and the policy forced off gives the trigger its grab back
    xrSetLightgun(c, LIGHTGUN_OFF, 0, 1);
    xrGrabStepAt(c, aim, 0, 0, 0.0f);
    xrGrabStepAt(c, aim, 1, 0, 0.0f);
    CHECK(c->grabMode == GRAB_MOVE);
    xrGrabStepAt(c, aim, 0, 0, 0.0f);
    useGun(c, 0);
    xrSetLightgun(c, LIGHTGUN_AUTO, 0, 1);
}

// ---- shots, reload and recoil -----------------------------------------------------------------------------

static void shots(Scene* s) {
    XrCtx* c = s->ctx;
    scene_reset(s);
    c->buttonsDown = 0;
    c->recoilPulses = 0;
    c->triggerEdge[0] = c->triggerEdge[1] = 0;

    // the gun with reload and recoil on
    useGun(c, 1);
    xrSetLightgun(c, LIGHTGUN_AUTO, 1, 1);
    CHECK(xrButtonsFrame(c, 0, HOVER_SCREEN, 1) == 0);
    // a shot at the picture: the left button, and a pulse once
    CHECK(xrButtonsFrame(c, VR_BUTTON_LEFT, HOVER_SCREEN, 1) == VR_BUTTON_LEFT);
    CHECK(c->recoilPulses == 1);
    CHECK(xrButtonsFrame(c, VR_BUTTON_LEFT, HOVER_SCREEN, 1) == VR_BUTTON_LEFT);
    CHECK(c->recoilPulses == 1);
    CHECK(xrButtonsFrame(c, 0, HOVER_SCREEN, 1) == 0);
    // a pull off the picture, in the room, on the margin or under it: a right click held with the trigger
    static const int clear[3] = { HOVER_NONE, HOVER_HALO, HOVER_BAR };
    for (int i = 0; i < 3; i++) {
        long pulses = c->recoilPulses;
        CHECK(xrButtonsFrame(c, VR_BUTTON_LEFT, clear[i], 0) == VR_BUTTON_RIGHT);
        CHECK(c->recoilPulses == pulses + 1);
        CHECK(xrButtonsFrame(c, VR_BUTTON_LEFT, clear[i], 0) == VR_BUTTON_RIGHT);
        // the aim swinging back onto the picture mid pull does not turn it into a left click
        CHECK(xrButtonsFrame(c, VR_BUTTON_LEFT, HOVER_SCREEN, 1) == VR_BUTTON_RIGHT);
        CHECK(c->recoilPulses == pulses + 1);
        CHECK(xrButtonsFrame(c, 0, HOVER_NONE, 0) == 0);
    }
    // a pull on something of ours (a button of the bar) is neither
    CHECK(xrButtonsFrame(c, VR_BUTTON_LEFT, HOVER_COGBUTTON, 0) == 0);
    CHECK(xrButtonsFrame(c, 0, HOVER_COGBUTTON, 0) == 0);

    // reload off: a pull off the picture does nothing and does not recoil
    xrSetLightgun(c, LIGHTGUN_AUTO, 0, 1);
    long pulses = c->recoilPulses;
    CHECK(xrButtonsFrame(c, VR_BUTTON_LEFT, HOVER_NONE, 0) == 0);
    CHECK(c->recoilPulses == pulses);
    CHECK(xrButtonsFrame(c, 0, HOVER_NONE, 0) == 0);
    // recoil off: the shot goes and nothing pulses
    xrSetLightgun(c, LIGHTGUN_AUTO, 1, 0);
    CHECK(xrButtonsFrame(c, VR_BUTTON_LEFT, HOVER_SCREEN, 1) == VR_BUTTON_LEFT);
    CHECK(c->recoilPulses == pulses);
    CHECK(xrButtonsFrame(c, 0, HOVER_SCREEN, 1) == 0);

    // without the policy: the pointer as it always was, whatever the extras say
    useGun(c, 0);
    xrSetLightgun(c, LIGHTGUN_AUTO, 1, 1);
    CHECK(xrButtonsFrame(c, VR_BUTTON_LEFT, HOVER_NONE, 0) == 0);
    CHECK(xrButtonsFrame(c, 0, HOVER_NONE, 0) == 0);
    CHECK(xrButtonsFrame(c, VR_BUTTON_LEFT, HOVER_SCREEN, 1) == VR_BUTTON_LEFT);
    CHECK(xrButtonsFrame(c, 0, HOVER_SCREEN, 1) == 0);
    CHECK(c->recoilPulses == pulses);
}

// ---- the gamepad's gyro ----------------------------------------------------------------------------------

// What the fake runtime says each space is doing
typedef struct {
    int ok;
    XrSpaceLocationFlags flags;
    XrQuaternionf q;
    XrVector3f w;
    int velocity;
} Fake;
static Fake fake[4]; // left aim, right aim, view
static int locates;
static int touch;

static int fakeIndex(XrSpace space) {
    return space == LEFT_AIM ? 0 : space == RIGHT_AIM ? 1 : space == VIEW ? 2 : -1;
}

static XrResult gyroLocate(XrCtx* ctx, XrSpace space, XrSpace base, XrTime time, XrSpaceLocation* out) {
    (void)ctx; (void)base; (void)time;
    int i = fakeIndex(space);
    locates++;
    if (i < 0 || !fake[i].ok) {
        return XR_ERROR_RUNTIME_FAILURE;
    }
    out->locationFlags = fake[i].flags;
    out->pose.orientation = fake[i].q;
    out->pose.position = (XrVector3f){ 0, 0, 0 };
    XrSpaceVelocity* v = (XrSpaceVelocity*)out->next;
    if (v != NULL && v->type == XR_TYPE_SPACE_VELOCITY) {
        v->velocityFlags = fake[i].velocity ? XR_SPACE_VELOCITY_ANGULAR_VALID_BIT : 0;
        v->angularVelocity = fake[i].w;
    }
    return XR_SUCCESS;
}

static int gyroBool(XrCtx* ctx, XrAction action, int hand) {
    (void)ctx;
    return action == TOUCH_ACTION && hand == HAND_RIGHT ? touch : 0;
}

static XrSpaceLocationFlags tracked(void) {
    return XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT
            | XR_SPACE_LOCATION_POSITION_VALID_BIT;
}

// What the sink was handed
typedef struct {
    int kind; // 1 arrival, 2 state, 3 motion
    int number;
    int type;
    int caps;
    int motionType;
    float v[3];
} Sent;
static Sent sent[256];
static int sentCount;

static int sinkArrival(void* user, uint8_t n, uint16_t mask, uint8_t type, uint32_t buttons, uint16_t caps) {
    (void)user; (void)mask; (void)buttons;
    if (sentCount < 256) {
        sent[sentCount++] = (Sent){ 1, n, type, caps, 0, { 0, 0, 0 } };
    }
    return 0;
}
static int sinkState(void* user, short n, short mask, int flags, unsigned char lt, unsigned char rt,
                     short lx, short ly, short rx, short ry) {
    (void)user; (void)n; (void)mask; (void)flags; (void)lt; (void)rt; (void)lx; (void)ly; (void)rx; (void)ry;
    return 0;
}
static int sinkMotion(void* user, uint8_t n, uint8_t type, float x, float y, float z) {
    (void)user;
    if (sentCount < 256) {
        sent[sentCount++] = (Sent){ 3, n, 0, 0, type, { x, y, z } };
    }
    return 0;
}

static int countKind(int kind) {
    int n = 0;
    for (int i = 0; i < sentCount; i++) n += sent[i].kind == kind;
    return n;
}

static const Sent* lastMotion(int type) {
    for (int i = sentCount - 1; i >= 0; i--) {
        if (sent[i].kind == 3 && sent[i].motionType == type) return &sent[i];
    }
    return NULL;
}

static int lastArrivalCaps(void) {
    for (int i = sentCount - 1; i >= 0; i--) {
        if (sent[i].kind == 1) return sent[i].caps;
    }
    return -1;
}

static int64_t clockNs;
static const int64_t FRAME = 11111111LL;

// One input frame of the pad on the host and its gyro
static void frame(XrCtx* c, int plugged, int live) {
    PadState rest;
    padRest(&rest);
    clockNs += FRAME;
    xrPadBridgeFrame(plugged, &rest, clockNs);
    xrPadGyroFrame(c, plugged, live, clockNs);
}

static void startPad(XrCtx* c, int source, int ratchet) {
    PadOutSink sink = { sinkArrival, sinkState, NULL, sinkMotion };
    xrPadBridgeStop();
    sentCount = 0;
    clockNs = 1000000000LL;
    xrSetPadGyro(c, source, ratchet); // before the stream starts, as the settings are read
    xrPadBridgeStartWithSink(&sink);
    locates = 0;
    touch = 0;
}

static void gyroSetUp(XrCtx* c) {
    memset(fake, 0, sizeof(fake));
    for (int i = 0; i < 3; i++) {
        fake[i].ok = 1;
        fake[i].flags = tracked();
        fake[i].q = (XrQuaternionf){ 0, 0, 0, 1 };
    }
    c->viewSpace = VIEW;
    c->localSpace = LOCAL;
    c->aimSpaces[0] = LEFT_AIM;
    c->aimSpaces[1] = RIGHT_AIM;
    c->profileKind[0] = PROFILE_CONTROLLER;
    c->profileKind[1] = PROFILE_CONTROLLER;
    c->locateSpace = gyroLocate;
    c->readBool = gyroBool;
    c->stickTouchAction = XR_NULL_HANDLE;
    c->stickTouchBound = 0;
    c->onFrame[HAND_LEFT] = c->onFrame[HAND_RIGHT] = 0;
}

static void gyroOff(Scene* s) {
    XrCtx* c = s->ctx;
    gyroSetUp(c);
    // off (the default): the Xbox pad it always was, and nothing is read or sent however the host asks
    startPad(c, PAD_GYRO_OFF, PAD_GYRO_RATCHET_ALWAYS);
    frame(c, 1, 1);
    CHECK(countKind(1) == 1);
    CHECK(sent[0].type == PADOUT_TYPE && sent[0].caps == PADOUT_CAPS);
    xrPadBridgeHostMotionState(0, PADOUT_MOTION_GYRO, 100);
    xrPadBridgeHostMotionState(0, PADOUT_MOTION_ACCEL, 100);
    CHECK(xrPadBridgeMotionRate(PADOUT_MOTION_GYRO) == 0);
    fake[1].w = (XrVector3f){ 0, 1, 0 };
    fake[1].velocity = 1;
    for (int i = 0; i < 10; i++) frame(c, 1, 1);
    CHECK(countKind(3) == 0 && locates == 0);
    xrPadBridgeStop();
}

static void gyroController(Scene* s) {
    XrCtx* c = s->ctx;
    gyroSetUp(c);
    startPad(c, PAD_GYRO_CONTROLLER, PAD_GYRO_RATCHET_ALWAYS);
    // the opt-in is on: the Frame pad says it is a PlayStation pad with a gyroscope and accelerometer
    frame(c, 1, 1);
    CHECK(countKind(1) == 1 && sent[0].type == PADOUT_TYPE_MOTION && sent[0].caps == PADOUT_CAPS_MOTION);
    // until the host asks nothing is read, let alone sent
    for (int i = 0; i < 5; i++) frame(c, 1, 1);
    CHECK(countKind(3) == 0 && locates == 0);
    // another pad's number is not ours
    xrPadBridgeHostMotionState(5, PADOUT_MOTION_GYRO, 100);
    CHECK(xrPadBridgeMotionRate(PADOUT_MOTION_GYRO) == 0);
    xrPadBridgeHostMotionState(0, PADOUT_MOTION_GYRO, 100);
    xrPadBridgeHostMotionState(0, PADOUT_MOTION_ACCEL, 100);
    CHECK(xrPadBridgeMotionRate(PADOUT_MOTION_GYRO) == 100 && xrPadBridgeMotionRate(PADOUT_MOTION_ACCEL) == 100);

    // the right controller turning to the right at half a radian a second, held level
    fake[1].w = (XrVector3f){ 0, -0.5f, 0 };
    fake[1].velocity = 1;
    frame(c, 1, 1);
    const Sent* g = lastMotion(PADOUT_MOTION_GYRO);
    const Sent* a = lastMotion(PADOUT_MOTION_ACCEL);
    CHECK(g != NULL && a != NULL);
    if (g != NULL) {
        CHECK_NEAR(g->v[0], 0.0, 1e-3);
        CHECK_NEAR(g->v[1], -0.5 * DEG, 1e-2);
        CHECK_NEAR(g->v[2], 0.0, 1e-3);
        CHECK(g->number == 0);
    }
    if (a != NULL) {
        CHECK_NEAR(a->v[0], 0.0, 1e-4);
        CHECK_NEAR(a->v[1], 9.80665, 1e-4);
        CHECK_NEAR(a->v[2], 0.0, 1e-4);
    }
    // the same sample is not sent again
    int n = countKind(3);
    frame(c, 1, 1);
    CHECK(countKind(3) == n);
    // the controller turned a quarter turn to the left: the same world spin is a spin about the device's z
    fake[1].q = quatNorm(axisAngleQuat((Vec3){ 0, 1, 0 }, (float)M_PI / 2.0f));
    fake[1].w = (XrVector3f){ 1, 0, 0 };
    frame(c, 1, 1);
    g = lastMotion(PADOUT_MOTION_GYRO);
    CHECK(g != NULL && fabs(g->v[2] - DEG) < 1e-2 && fabs(g->v[0]) < 1e-2);
    // and gravity is still up for the world, which the device now sees on its y
    a = lastMotion(PADOUT_MOTION_ACCEL);
    CHECK(a != NULL && fabs(a->v[1] - 9.80665) < 1e-3);

    // the right controller not tracked: the left one's turn is sent
    fake[1].ok = 0;
    fake[0].w = (XrVector3f){ 0.25f, 0, 0 };
    fake[0].velocity = 1;
    fake[0].q = (XrQuaternionf){ 0, 0, 0, 1 };
    frame(c, 1, 1);
    g = lastMotion(PADOUT_MOTION_GYRO);
    CHECK(g != NULL && fabs(g->v[0] - 0.25 * DEG) < 1e-2);
    // neither: a still gyroscope, and the accelerometer left as it was
    fake[0].ok = 0;
    n = countKind(3);
    frame(c, 1, 1);
    g = lastMotion(PADOUT_MOTION_GYRO);
    CHECK(g != NULL && g->v[0] == 0.0f && g->v[1] == 0.0f && g->v[2] == 0.0f);
    CHECK(countKind(3) == n + 1);
    // a controller that is only valid, not tracked, is not a source
    fake[1].ok = 1;
    fake[1].flags = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    fake[1].w = (XrVector3f){ 0, 1, 0 };
    frame(c, 1, 1);
    g = lastMotion(PADOUT_MOTION_GYRO);
    CHECK(g != NULL && g->v[1] == 0.0f);
    fake[1].flags = tracked();
    fake[0].ok = 1;

    // a panel up (not live): still, then nothing new
    fake[1].q = (XrQuaternionf){ 0, 0, 0, 1 };
    frame(c, 1, 1);
    CHECK(lastMotion(PADOUT_MOTION_GYRO)->v[1] != 0.0f);
    n = countKind(3);
    frame(c, 1, 0);
    CHECK(lastMotion(PADOUT_MOTION_GYRO)->v[1] == 0.0f && countKind(3) == n + 1);
    frame(c, 1, 0);
    CHECK(countKind(3) == n + 1);
    frame(c, 1, 1);
    CHECK(lastMotion(PADOUT_MOTION_GYRO)->v[1] != 0.0f);

    // pointer mode (the pad off the host): nothing is read, the requests go with the pad
    n = countKind(3);
    locates = 0;
    frame(c, 0, 0);
    frame(c, 0, 0);
    CHECK(countKind(3) == n && locates == 0);
    CHECK(xrPadBridgeMotionRate(PADOUT_MOTION_GYRO) == 0);
    // back in gamepad mode it is a new pad, announced again, and nothing goes until the host asks again
    frame(c, 1, 1);
    CHECK(countKind(1) == 2 && lastArrivalCaps() == PADOUT_CAPS_MOTION);
    n = countKind(3);
    frame(c, 1, 1);
    CHECK(countKind(3) == n);
    xrPadBridgeStop();
}

static void gyroHead(Scene* s) {
    XrCtx* c = s->ctx;
    gyroSetUp(c);
    startPad(c, PAD_GYRO_HEAD, PAD_GYRO_RATCHET_ALWAYS);
    frame(c, 1, 1);
    xrPadBridgeHostMotionState(0, PADOUT_MOTION_GYRO, 250);
    xrPadBridgeHostMotionState(0, PADOUT_MOTION_ACCEL, 250);
    // the head's turn, with the controllers doing something else altogether
    fake[2].w = (XrVector3f){ 0.2f, 0, 0 };
    fake[2].velocity = 1;
    fake[1].w = (XrVector3f){ 0, 3, 0 };
    fake[1].velocity = 1;
    frame(c, 1, 1);
    const Sent* g = lastMotion(PADOUT_MOTION_GYRO);
    CHECK(g != NULL && fabs(g->v[0] - 0.2 * DEG) < 1e-2 && fabs(g->v[1]) < 1e-2);
    // the head pitched down 30 degrees: gravity is turned into its frame
    fake[2].q = quatNorm(axisAngleQuat((Vec3){ 1, 0, 0 }, -(float)M_PI / 6.0f));
    frame(c, 1, 1);
    const Sent* a = lastMotion(PADOUT_MOTION_ACCEL);
    CHECK(a != NULL);
    if (a != NULL) {
        float len = sqrtf(a->v[0] * a->v[0] + a->v[1] * a->v[1] + a->v[2] * a->v[2]);
        CHECK_NEAR(len, 9.80665, 1e-3);
        CHECK_NEAR(a->v[1], 9.80665 * cos(M_PI / 6.0), 1e-3);
        CHECK(fabs(a->v[2]) > 4.0f);
    }
    // a runtime that gives no velocity: the turn between two samples instead
    fake[2].velocity = 0;
    fake[2].q = (XrQuaternionf){ 0, 0, 0, 1 };
    frame(c, 1, 1);
    fake[2].q = quatNorm(axisAngleQuat((Vec3){ 0, 1, 0 }, 0.01f));
    frame(c, 1, 1);
    g = lastMotion(PADOUT_MOTION_GYRO);
    CHECK(g != NULL && fabs(g->v[1] - 0.01 / (FRAME * 1e-9) * DEG) < 0.5 && fabs(g->v[0]) < 0.1);
    // the head not placed: still
    fake[2].ok = 0;
    frame(c, 1, 1);
    g = lastMotion(PADOUT_MOTION_GYRO);
    CHECK(g != NULL && g->v[0] == 0.0f && g->v[1] == 0.0f && g->v[2] == 0.0f);
    xrPadBridgeStop();
}

static void gyroRatchet(Scene* s) {
    XrCtx* c = s->ctx;
    gyroSetUp(c);
    // stick_touch with the touch sensor read (bound on the Frame profile, the right hand on it):
    // motion only while the thumb is on the stick
    c->stickTouchAction = TOUCH_ACTION;
    c->stickTouchBound = 1;
    c->onFrame[HAND_RIGHT] = 1;
    startPad(c, PAD_GYRO_CONTROLLER, PAD_GYRO_RATCHET_STICK_TOUCH);
    frame(c, 1, 1);
    xrPadBridgeHostMotionState(0, PADOUT_MOTION_GYRO, 100);
    fake[1].w = (XrVector3f){ 0, 0.5f, 0 };
    fake[1].velocity = 1;
    touch = 0;
    frame(c, 1, 1);
    // never anything sent but a still gyroscope while the thumb is off, and only once
    int n = countKind(3);
    frame(c, 1, 1);
    frame(c, 1, 1);
    CHECK(countKind(3) == n);
    CHECK(n == 0 || (lastMotion(PADOUT_MOTION_GYRO)->v[1] == 0.0f));
    touch = 1;
    frame(c, 1, 1);
    const Sent* g = lastMotion(PADOUT_MOTION_GYRO);
    CHECK(g != NULL && fabs(g->v[1] - 0.5 * DEG) < 1e-2);
    // the thumb comes off: the host is told the gyroscope is still, rather than left with the last turn
    touch = 0;
    n = countKind(3);
    frame(c, 1, 1);
    g = lastMotion(PADOUT_MOTION_GYRO);
    CHECK(countKind(3) == n + 1 && g->v[0] == 0.0f && g->v[1] == 0.0f && g->v[2] == 0.0f);
    frame(c, 1, 1);
    CHECK(countKind(3) == n + 1);
    xrPadBridgeStop();

    // the right hand leaves the Frame profile (another profile, or hands): the binding no longer
    // reads, so the thumb off the stick does not hold the gyro still; back on it, the ratchet again
    startPad(c, PAD_GYRO_CONTROLLER, PAD_GYRO_RATCHET_STICK_TOUCH);
    frame(c, 1, 1);
    xrPadBridgeHostMotionState(0, PADOUT_MOTION_GYRO, 100);
    touch = 0;
    c->onFrame[HAND_RIGHT] = 0;
    frame(c, 1, 1);
    g = lastMotion(PADOUT_MOTION_GYRO);
    CHECK(g != NULL && fabs(g->v[1] - 0.5 * DEG) < 1e-2);
    CHECK(c->padGyroSaidFallback == 1);
    c->onFrame[HAND_RIGHT] = 1;
    frame(c, 1, 1);
    g = lastMotion(PADOUT_MOTION_GYRO);
    CHECK(g != NULL && g->v[1] == 0.0f);
    CHECK(c->padGyroSaidFallback == 0);
    xrPadBridgeStop();

    // the action exists but no Frame profile took its binding: always, with a line in the log
    c->stickTouchBound = 0;
    startPad(c, PAD_GYRO_CONTROLLER, PAD_GYRO_RATCHET_STICK_TOUCH);
    frame(c, 1, 1);
    xrPadBridgeHostMotionState(0, PADOUT_MOTION_GYRO, 100);
    frame(c, 1, 1);
    g = lastMotion(PADOUT_MOTION_GYRO);
    CHECK(g != NULL && fabs(g->v[1] - 0.5 * DEG) < 1e-2);
    CHECK(c->padGyroSaidFallback == 1);
    xrPadBridgeStop();

    // stick_touch where the touch sensor is not read: always, with a line in the log (said once)
    c->stickTouchAction = XR_NULL_HANDLE;
    startPad(c, PAD_GYRO_CONTROLLER, PAD_GYRO_RATCHET_STICK_TOUCH);
    frame(c, 1, 1);
    xrPadBridgeHostMotionState(0, PADOUT_MOTION_GYRO, 100);
    frame(c, 1, 1);
    g = lastMotion(PADOUT_MOTION_GYRO);
    CHECK(g != NULL && fabs(g->v[1] - 0.5 * DEG) < 1e-2);
    CHECK(c->padGyroSaidFallback == 1);
    xrPadBridgeStop();
}

static void gyroRate(Scene* s) {
    XrCtx* c = s->ctx;
    gyroSetUp(c);
    startPad(c, PAD_GYRO_CONTROLLER, PAD_GYRO_RATCHET_ALWAYS);
    frame(c, 1, 1);
    // the host asks for 25 Hz: every fourth frame of a 90 Hz loop, never faster
    xrPadBridgeHostMotionState(0, PADOUT_MOTION_GYRO, 25);
    fake[1].velocity = 1;
    int before = countKind(3);
    for (int i = 0; i < 90; i++) {
        fake[1].w = (XrVector3f){ 0, 0.001f * (float)(i + 1), 0 }; // a different sample every frame
        frame(c, 1, 1);
    }
    int sentN = countKind(3) - before;
    CHECK(sentN >= 21 && sentN <= 24);
    xrPadBridgeStop();
}

int main(void) {
    Scene* s = scene_new();
    followsModel(s);
    pointerSleep(s);
    gripOnlyGrab(s);
    shots(s);
    gyroOff(s);
    gyroController(s);
    gyroHead(s);
    gyroRatchet(s);
    gyroRate(s);
    xrSetPadGyro(s->ctx, PAD_GYRO_OFF, PAD_GYRO_RATCHET_ALWAYS);
    scene_free(s);
    return checksDone("m4 lightgun");
}
