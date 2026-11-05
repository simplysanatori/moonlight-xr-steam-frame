// Moonlight XR Native, tests/m4: the keyboard's move bar and the 3D tab's live
// sliders (owner's requests). The bar: a ray at the pill under the keyboard
// is HOVER_KBBAR, grabbing it carries the keyboard with the hand in place and
// turn, the thumbsticks push it away and closer, letting go leaves it, the
// keys are still hit at the new pose, a keyboard never moved is where it
// always was, and resetting the screen's placement brings a moved one back.
// The sliders: dragging the depth or convergence track moves what the picture
// is drawn with and what the panel is drawn from before it is let go, while
// config.ini keeps the old value until then.
// Native (lane grab-pull): the picture held by its move bar, and the keyboard
// by its own, come nearer on a stick pulled south as fast as they go away on
// one pushed north, a slanted pull counts in full, and a pull held at the
// least distance is not stored up against the next push. GPLv3.
#include "check.h"
#include "scene.h"
#include "xr_depth.h"
#include "xr_keyboard.h"

static XrPosef aimAt(Vec3 from, Vec3 target) {
    Vec3 d = vecNorm(vecSub(target, from));
    Vec3 z = { -d.x, -d.y, -d.z };
    Vec3 up = { 0, 1, 0 };
    Vec3 x = vecNorm(vecCross(up, z));
    Vec3 y = vecCross(z, x);
    XrPosef p = { quatFromBasis(x, y, z), { from.x, from.y, from.z } };
    return p;
}

static Vec3 onPlane(XrPosef pose, float w, float h, float u, float v) {
    Vec3 local = { (u - 0.5f) * w, (0.5f - v) * h, 0.0f };
    Vec3 off = quatRotate(pose.orientation, local);
    Vec3 p = { pose.position.x + off.x, pose.position.y + off.y, pose.position.z + off.z };
    return p;
}

static Vec3 pos(XrPosef p) {
    Vec3 v = { p.position.x, p.position.y, p.position.z };
    return v;
}

static float dist(Vec3 a, Vec3 b) {
    Vec3 d = vecSub(a, b);
    return sqrtf(vecDot(d, d));
}

// Where the keyboard's pill is, in the space
static Vec3 pillPoint(XrCtx* c) {
    float w, h, drop;
    kbBarGeometry(c, &w, &h, &drop);
    return onPlane(c->kbPose, c->kbW, c->kbH, 0.5f, 0.5f + drop / c->kbH);
}

// The angle between the keyboards' facing in two poses, degrees
static float turnedDeg(XrPosef a, XrPosef b) {
    Vec3 n = { 0, 0, 1 };
    Vec3 na = quatRotate(a.orientation, n), nb = quatRotate(b.orientation, n);
    float d = vecDot(na, nb);
    if (d > 1.0f) d = 1.0f;
    if (d < -1.0f) d = -1.0f;
    return acosf(d) * 180.0f / (float)M_PI;
}

static const Vec3 HAND = { 0.25f, -0.35f, -0.3f };

// A key's centre in the space, through the keyboard's pose now
static Vec3 keyPoint(XrCtx* c, int key) {
    const float* r = &c->kbKeyRects[key * 4];
    return onPlane(c->kbPose, c->kbW, c->kbH, (r[0] + r[2]) * 0.5f, (r[1] + r[3]) * 0.5f);
}

// Every key of the sheet showing is hit at the keyboard's pose now, from the
// hand, and is the key that was aimed at
static int keysHit(XrCtx* c) {
    int hit = 0;
    for (int k = 0; k < c->kbKeyCount; k++) {
        if (c->kbCodes[c->kbState][k] == 0) continue;
        float u, v;
        int hover = xrHoverAt(c, aimAt(HAND, keyPoint(c, k)), &u, &v, NULL);
        if (hover != HOVER_KBPANEL) return 0;
        if (kbKeyAt(c, u, v) != k) return 0;
        hit++;
    }
    return hit;
}

static void defaults(Scene* s) {
    XrCtx* c = s->ctx;
    scene_reset(s);
    c->headPos = (XrVector3f){ 0, 0, 0 };
    c->kbOpen = 1;
    // Never moved: where it always was, and the same size
    XrPosef was = kbPanelPose(c, &c->kbW, &c->kbH);
    kbPlace(c);
    CHECK(!c->kbMoved);
    CHECK(c->kbPose.position.x == was.position.x && c->kbPose.position.y == was.position.y
          && c->kbPose.position.z == was.position.z);
    CHECK(c->kbPose.orientation.w == was.orientation.w && c->kbPose.orientation.x == was.orientation.x);
    // and its bar hangs under it, where the picture's hangs under the picture
    float w, h, drop;
    kbBarGeometry(c, &w, &h, &drop);
    CHECK_NEAR(w, c->screenWidth * BAR_WIDTH_FRAC, 1e-4);
    CHECK_NEAR(h, c->screenWidth * BAR_HEIGHT_FRAC, 1e-4);
    CHECK_NEAR(drop, c->kbH * 0.5f + c->screenWidth * BAR_DROP_FRAC, 1e-4);
    CHECK(keysHit(c) > 30);
    Vec3 eye = { 0, 0, 0 };
    CHECK(xrHoverAt(c, aimAt(eye, pillPoint(c)), NULL, NULL, NULL) == HOVER_KBBAR);
    CHECK(xrHoverAt(c, aimAt(HAND, pillPoint(c)), NULL, NULL, NULL) == HOVER_KBBAR);
    // not far to either side of it
    Vec3 off = onPlane(c->kbPose, c->kbW, c->kbH, 0.5f + 0.5f * (w * 4.0f) / c->kbW, 0.5f + drop / c->kbH);
    CHECK(xrHoverAt(c, aimAt(eye, off), NULL, NULL, NULL) != HOVER_KBBAR);
    // the Full layout's bar is the same size
    c->kbLayout = KB_LAYOUT_FULL;
    xrKeyboardSync(c, NULL);
    kbPlace(c);
    float fw, fh, fdrop;
    kbBarGeometry(c, &fw, &fh, &fdrop);
    CHECK_NEAR(fw, w, 1e-4);
    c->kbLayout = KB_LAYOUT_COMPACT;
    xrKeyboardSync(c, NULL);
    kbPlace(c);
    // closed, the bar is nobody's
    c->kbOpen = 0;
    CHECK(xrHoverAt(c, aimAt(eye, pillPoint(c)), NULL, NULL, NULL) != HOVER_KBBAR);
    c->kbOpen = 1;
}

static void grabAndMove(Scene* s) {
    XrCtx* c = s->ctx;
    scene_reset(s);
    c->headPos = (XrVector3f){ 0, 0, 0 };
    c->kbOpen = 1;
    c->kbMoved = 0;
    kbPlace(c);
    XrPosef start = c->kbPose;
    XrPosef screen = c->screenPose;

    // The trigger on the pill takes hold of it, and nothing else
    XrPosef aim = aimAt(HAND, pillPoint(c));
    CHECK(xrGrabStepAt(c, aim, 0, 0, 0.0f) == HOVER_KBBAR);
    CHECK(c->grabMode == GRAB_NONE);
    CHECK(xrGrabStepAt(c, aim, 1, 0, 0.0f) == HOVER_KBBAR);
    CHECK(c->grabMode == GRAB_KBMOVE && c->kbMoved);
    // A frame later it has not jumped
    CHECK(xrGrabStepAt(c, aim, 1, 0, 0.0f) != HOVER_NONE || c->grabMode == GRAB_KBMOVE);
    CHECK(dist(pos(c->kbPose), pos(start)) < 1e-4f && turnedDeg(c->kbPose, start) < 0.01f);

    // The hand carries it along: the same move, the same way
    XrPosef carried = aim;
    carried.position.x += 0.30f;
    carried.position.y += 0.12f;
    carried.position.z -= 0.10f;
    xrGrabStepAt(c, carried, 1, 0, 0.0f);
    CHECK(c->grabMode == GRAB_KBMOVE);
    CHECK_NEAR(c->kbPose.position.x - start.position.x, 0.30, 1e-3);
    CHECK_NEAR(c->kbPose.position.y - start.position.y, 0.12, 1e-3);
    CHECK_NEAR(c->kbPose.position.z - start.position.z, -0.10, 1e-3);
    CHECK(turnedDeg(c->kbPose, start) < 0.01f);
    // and the picture stays where it is
    CHECK(dist(pos(c->screenPose), pos(screen)) == 0.0f);

    // The wrist turned down 60 degrees lays it at an angle: it turns by as
    // much, and swings about the hand
    Vec3 xAxis = { 1, 0, 0 };
    XrPosef tilted = carried;
    tilted.orientation = quatNorm(quatMul(axisAngleQuat(xAxis, -60.0f * (float)M_PI / 180.0f), carried.orientation));
    xrGrabStepAt(c, tilted, 1, 0, 0.0f);
    XrPosef carriedPose = start;
    carriedPose.position.x += 0.30f;
    CHECK_NEAR(turnedDeg(c->kbPose, start), 60.0, 0.5);
    // Rigid about the hand: its distance from the hand is what it was
    CHECK_NEAR(dist(pos(c->kbPose), pos(tilted)), dist(pos(start), pos(aim)), 1e-3);
    XrPosef laid = c->kbPose;
    c->kbHoverKey = 5;

    // North on a thumbstick sends it away from the head, south brings it back
    float before = dist(pos(c->kbPose), (Vec3){ 0, 0, 0 });
    for (int i = 0; i < 45; i++) xrGrabStepAt(c, tilted, 1, 0, 1.0f);
    float away = dist(pos(c->kbPose), (Vec3){ 0, 0, 0 });
    CHECK(away > before + 0.4f);
    for (int i = 0; i < 90; i++) xrGrabStepAt(c, tilted, 1, 0, -1.0f);
    float closer = dist(pos(c->kbPose), (Vec3){ 0, 0, 0 });
    CHECK(closer < away - 0.4f);
    CHECK(c->kbHoverKey == 5);   // the frame's pass clears it; the step has no frame
    // pushing does not turn it
    CHECK(turnedDeg(c->kbPose, laid) < 0.01f);
    // the least it will come to
    for (int i = 0; i < 400; i++) xrGrabStepAt(c, tilted, 1, 0, -1.0f);
    CHECK(dist(pos(c->kbPose), (Vec3){ 0, 0, 0 }) >= GRAB_PUSH_NEAR_M - 1e-3f);
    for (int i = 0; i < 45; i++) xrGrabStepAt(c, tilted, 1, 0, 1.0f);

    // Letting go leaves it there, and it keeps still after
    XrPosef left = c->kbPose;
    xrGrabStepAt(c, tilted, 0, 0, 0.0f);
    CHECK(c->grabMode == GRAB_NONE && !c->poseDirty);
    XrPosef elsewhere = tilted;
    elsewhere.position.x -= 0.5f;
    xrGrabStepAt(c, elsewhere, 0, 0, 0.0f);
    CHECK(dist(pos(c->kbPose), pos(left)) == 0.0f && turnedDeg(c->kbPose, left) < 0.01f);
    CHECK(dist(pos(c->screenPose), pos(screen)) == 0.0f);

    // Its keys are hit where it is now, at that angle, every one of them
    XrPosef at = c->kbPose;
    CHECK(turnedDeg(at, start) > 30.0f);
    c->headPos = (XrVector3f){ 0, 0, 0 };
    int hit = keysHit(c);
    CHECK(hit > 30);
    // and the pill moved with it
    Vec3 eye = { 0, 0, 0 };
    CHECK(xrHoverAt(c, aimAt(HAND, pillPoint(c)), NULL, NULL, NULL) == HOVER_KBBAR);
    (void)eye;

    // Shut and opened again it comes up where it was left, whatever layout
    c->kbOpen = 0;
    kbPlace(c);
    CHECK(dist(pos(c->kbPose), pos(at)) < 1e-5f && turnedDeg(c->kbPose, at) < 0.01f);
    c->kbOpen = 1;
    c->kbLayout = KB_LAYOUT_FULL;
    xrKeyboardSync(c, NULL);
    CHECK(dist(pos(c->kbPose), pos(at)) < 1e-5f && turnedDeg(c->kbPose, at) < 0.01f);
    CHECK(keysHit(c) > 60);
    c->kbLayout = KB_LAYOUT_COMPACT;
    xrKeyboardSync(c, NULL);

    // Taken again from where it is
    aim = aimAt(HAND, pillPoint(c));
    CHECK(xrGrabStepAt(c, aim, 1, 0, 0.0f) == HOVER_KBBAR);
    CHECK(c->grabMode == GRAB_KBMOVE);
    // the grip takes it as well
    xrGrabStepAt(c, aim, 0, 0, 0.0f);
    CHECK(c->grabMode == GRAB_NONE);
    CHECK(xrGrabStepAt(c, aim, 0, 1, 0.0f) == HOVER_KBBAR);
    CHECK(c->grabMode == GRAB_KBMOVE);
    xrGrabStepAt(c, aim, 0, 0, 0.0f);
    CHECK(c->grabMode == GRAB_NONE);

    // A ray on the keys is a press on a key, not a grab
    Vec3 key = keyPoint(c, 3);
    XrPosef onKey = aimAt(HAND, key);
    CHECK(xrGrabStepAt(c, onKey, 1, 0, 0.0f) == HOVER_KBPANEL);
    CHECK(c->grabMode == GRAB_NONE);
    xrGrabStepAt(c, onKey, 0, 0, 0.0f);
}

static void resets(Scene* s) {
    XrCtx* c = s->ctx;
    scene_reset(s);
    c->headPos = (XrVector3f){ 0, 0, 0 };
    c->kbOpen = 1;
    c->kbMoved = 0;
    kbPlace(c);
    XrPosef home = c->kbPose;
    XrPosef aim = aimAt(HAND, pillPoint(c));
    xrGrabStepAt(c, aim, 1, 0, 0.0f);
    aim.position.y += 0.4f;
    xrGrabStepAt(c, aim, 1, 0, 0.0f);
    xrGrabStepAt(c, aim, 0, 0, 0.0f);
    CHECK(c->kbMoved && dist(pos(c->kbPose), pos(home)) > 0.3f);

    // The screen's placement reset (the Screen tab's button drops it, and
    // the next frame reseeds it): the keyboard comes back under it, up
    c->placementValid = 0;
    float distance, width, curve;
    xrDefaultPlacement(&distance, &width, &curve);
    CHECK(updatePlacement(c, distance, width, curve));
    CHECK(!c->kbMoved);
    XrPosef def = kbPanelPose(c, &c->kbW, &c->kbH);
    CHECK(dist(pos(c->kbPose), pos(home)) < 1e-4f && turnedDeg(c->kbPose, home) < 0.01f);
    CHECK(dist(pos(c->kbPose), pos(def)) < 1e-5f && turnedDeg(c->kbPose, def) < 0.01f);

    // Recentring does the same, closed or not
    aim = aimAt(HAND, pillPoint(c));
    xrGrabStepAt(c, aim, 1, 0, 0.0f);
    aim.position.x -= 0.4f;
    xrGrabStepAt(c, aim, 1, 0, 0.0f);
    xrGrabStepAt(c, aim, 0, 0, 0.0f);
    CHECK(c->kbMoved);
    c->kbOpen = 0;
    recentreScreen(c);
    CHECK(!c->kbMoved);
    kbPlace(c);
    CHECK(dist(pos(c->kbPose), pos(kbPanelPose(c, &c->kbW, &c->kbH))) < 1e-5f);
    // and an unmoved one is left alone by it
    c->kbOpen = 1;
    kbPlace(c);
    XrPosef stay = c->kbPose;
    recentreScreen(c);
    CHECK(dist(pos(c->kbPose), pos(stay)) == 0.0f);
}

// A frame with the keyboard moved and angled still fits the layers
static void layers(Scene* s) {
    XrCtx* c = s->ctx;
    scene_reset(s);
    c->headPos = (XrVector3f){ 0, 0, 0 };
    c->kbOpen = 1;
    c->kbMoved = 0;
    kbPlace(c);
    c->panelFades[FADE_KB].level = 1.0f;
    c->overlayVisible = 1;
    s->settings.head_locked = 1;
    c->headLockedPref = 1;
    XrPosef aim = aimAt(HAND, pillPoint(c));
    xrGrabStepAt(c, aim, 1, 0, 0.0f);
    Vec3 xAxis = { 1, 0, 0 };
    aim.orientation = quatNorm(quatMul(axisAngleQuat(xAxis, -1.0f), aim.orientation));
    aim.position.y += 0.2f;
    xrGrabStepAt(c, aim, 1, 0, 0.0f);
    c->hoverKind = HOVER_KBBAR;
    c->kbHoverKey = 3;
    noticePush(&c->notices, TOAST_3D_ON, 0);
    Notice up;
    if (noticeAdvance(&c->notices, nowNs(), 0, &up)) xrPanelsToast(c, up.kind, up.arg);
    scene_frame(s, "stats");
    printf("keyboard moved and held, stats, toast, ray, head locked: %u layers (+1 passthrough)\n", s->out.count);
    CHECK(s->out.count + 1 <= 16);
    xrGrabStepAt(c, aim, 0, 0, 0.0f);
    s->settings.head_locked = 0;
    c->headLockedPref = 0;
}

// The 3D tab's tracks: live while dragged, written when let go
static void liveSliders(Scene* s) {
    XrCtx* c = s->ctx;
    scene_reset(s);
    c->stereoMode = DEPTH_MODE_EYETEST;
    c->cogOpen = 1;
    c->cogTab = COG_TAB_3D;
    Settings file = s->settings;
    int fileSep = settings_separation_units(&file);
    int fileConv = settings_convergence_percent(&file);
    c->separationPref = separationOf(fileSep);
    c->panelSeparation = -1.0f;
    c->convergence = (float)fileConv / 100.0f;
    xrDepthFrame(c);
    float drawnBefore = c->separationCurrent;
    CHECK_NEAR(drawnBefore, separationOf(fileSep), 1e-6);

    // The depth track taken to its far end
    c->cogDragSlider = COG_ROW3D_SEPARATION;
    c->cogDragFace = COG_TAB_3D;
    cogApplySlider(c, COG_TAB_3D, COG_ROW3D_SEPARATION, cogRunU(1.0f));
    int units = separationUnits(c->panelSeparation);
    CHECK(units == COG_SEP_STEPS && units != fileSep);
    // what the picture is drawn with, next frame, before the drag is over
    xrDepthFrame(c);
    CHECK_NEAR(c->separationCurrent, separationOf(COG_SEP_STEPS), 1e-6);
    CHECK(fabsf(c->separationCurrent - drawnBefore) > 1e-4f);
    // what the panel is drawn from, as the frame builds it
    Settings live = file;
    xrSettingsFromCtx(c, &live);
    CHECK(settings_separation_units(&live) == COG_SEP_STEPS);
    CHECK(settings_separation_units(&file) == fileSep);
    // and so a redraw of the panel, with the thumb at the end of the track
    CHECK(memcmp(&live, &file, sizeof(live)) != 0);

    // The convergence track likewise
    c->cogDragSlider = COG_ROW3D_CONVERGENCE;
    cogApplySlider(c, COG_TAB_3D, COG_ROW3D_CONVERGENCE, cogRunU(0.25f));
    CHECK(fabsf(c->convergence - (float)fileConv / 100.0f) > 0.05f);
    live = file;
    xrSettingsFromCtx(c, &live);
    CHECK(settings_convergence_percent(&live) == (int)lroundf(c->convergence * 100.0f));
    CHECK(settings_convergence_percent(&file) == fileConv);

    // Let go: the same values go to config.ini, and nothing else is changed
    float out[IN_SLOTS];
    memset(out, 0, sizeof(out));
    out[IN_SETTING] = -1.0f;
    cogDragEnded(c, out);
    CHECK((int)out[IN_SETTING] == SETTING_CONVERGENCE);
    CHECK((int)out[IN_SETTING_VALUE] == (int)lroundf(c->convergence * 100.0f));
    c->cogDragSlider = COG_ROW3D_SEPARATION;
    c->cogDragFace = COG_TAB_3D;
    memset(out, 0, sizeof(out));
    cogDragEnded(c, out);
    CHECK((int)out[IN_SETTING] == SETTING_SEPARATION && (int)out[IN_SETTING_VALUE] == COG_SEP_STEPS);

    // A file that leaves them to the model (-1) stays that while untouched
    c->panelSeparation = -1.0f;
    c->convergence = 0.5f;
    Settings model = s->settings;
    model.separation = -1;
    model.convergence = -1;
    live = model;
    xrSettingsFromCtx(c, &live);
    CHECK(live.separation == -1 && live.convergence == -1);

    // restore for the others
    c->cogOpen = 0;
    c->stereoMode = DEPTH_MODE_OFF;
    c->panelSeparation = -1.0f;
}

// ---- the picture's move bar: push and pull (lane grab-pull) ----------------

// The middle of the picture's move bar, where a grip takes it
static Vec3 pictureBarPoint(XrCtx* c) {
    BarFrame f = barFrame(c);
    Vec3 local = { 0.0f, -(f.height * 0.5f + f.width * BAR_DROP_FRAC), 0.0f };
    float yaw = 0.0f;
    curveLocal(&local, f.radius, f.curved, &yaw);
    Vec3 r = quatRotate(f.pose.orientation, local);
    Vec3 p = { f.pose.position.x + r.x, f.pose.position.y + r.y, f.pose.position.z + r.z };
    return p;
}

static float fromHead(XrPosef p) {
    return dist(pos(p), (Vec3){ 0, 0, 0 });
}

// One second of frames at the test hook's 90 Hz with the stick held as given
static void holdStick(XrCtx* c, XrPosef aim, float x, float y) {
    XrVector2f stick = { x, y };
    for (int i = 0; i < 90; i++) xrGrabStepStick(c, aim, 0, 1, stick);
}

// The owner's picture: 3 m out and 3.6 m wide, flat or wrapped round
static void pictureAt3m(Scene* s, float curve) {
    XrCtx* c = s->ctx;
    scene_reset(s);
    c->headPos = (XrVector3f){ 0, 0, 0 };
    c->kbOpen = 0;
    c->panelCurve = curve;
    memset(&c->screenPose, 0, sizeof(c->screenPose));
    c->screenPose.orientation.w = 1.0f;
    c->screenPose.position.z = -3.0f;
    c->screenWidth = 3.6f;
    c->screenRadius = 3.0f * (1.0f + 3.0f * (1.0f - curve));
    c->grabMode = GRAB_NONE;
}

static void pictureBarPushPull(Scene* s) {
    XrCtx* c = s->ctx;
    XrPosef keepPose = c->screenPose;
    float keepW = c->screenWidth, keepR = c->screenRadius, keepCurve = c->panelCurve;
    const float second = GRAB_PUSH_SPEED;   // metres one second at full tilt moves it

    for (int curved = 0; curved < 2; curved++) {
        pictureAt3m(s, curved ? 1.0f : 0.0f);
        XrPosef aim = aimAt(HAND, pictureBarPoint(c));
        CHECK(xrGrabStepAt(c, aim, 0, 0, 0.0f) == HOVER_BAR);
        // The grip on the bar takes the picture, and a still hand leaves it put
        CHECK(xrGrabStepAt(c, aim, 0, 1, 0.0f) == HOVER_BAR);
        CHECK(c->grabMode == GRAB_MOVE);
        xrGrabStepAt(c, aim, 0, 1, 0.0f);
        CHECK_NEAR(fromHead(c->screenPose), 3.0, 1e-3);

        // A stick inside the dead zone, either way or slanted, does nothing
        holdStick(c, aim, 0.0f, 0.15f);
        holdStick(c, aim, 0.0f, -0.15f);
        holdStick(c, aim, 0.12f, -0.12f);
        CHECK_NEAR(fromHead(c->screenPose), 3.0, 1e-4);
        // Nor does one pushed east or west
        holdStick(c, aim, 1.0f, 0.3f);
        holdStick(c, aim, -1.0f, -0.3f);
        CHECK_NEAR(fromHead(c->screenPose), 3.0, 1e-4);

        // North for a second sends it away, south for a second brings it back
        // by the same, to the millimetre
        holdStick(c, aim, 0.0f, 1.0f);
        float away = fromHead(c->screenPose);
        CHECK_NEAR(away - 3.0f, second, 0.01);
        holdStick(c, aim, 0.0f, -1.0f);
        float back = fromHead(c->screenPose);
        CHECK_NEAR(away - back, away - 3.0f, 1e-3);
        CHECK_NEAR(back, 3.0, 1e-3);
        // and on past where it started, nearer still
        holdStick(c, aim, 0.0f, -1.0f);
        CHECK_NEAR(fromHead(c->screenPose), 3.0f - second, 0.01);
        holdStick(c, aim, 0.0f, 1.0f);
        CHECK_NEAR(fromHead(c->screenPose), 3.0, 1e-3);

        // A thumb drawn back toward the palm goes south on a slant and short
        // of the rim: that is a full pull too, as fast as a full push
        holdStick(c, aim, -0.62f, -0.62f);
        CHECK_NEAR(3.0f - fromHead(c->screenPose), second, 0.01);
        holdStick(c, aim, 0.0f, 0.9f);
        CHECK_NEAR(fromHead(c->screenPose), 3.0, 1e-3);
        // Half a tilt is slower than a full one, the same either way
        holdStick(c, aim, 0.0f, 0.5f);
        float half = fromHead(c->screenPose) - 3.0f;
        CHECK(half > 0.05f && half < second * 0.5f);
        holdStick(c, aim, 0.0f, -0.5f);
        CHECK_NEAR(fromHead(c->screenPose), 3.0, 1e-3);

        // Pulled in to the least it comes to, it stays there however long
        // the pull is held, and a push takes it straight back out: nothing of
        // the pull was stored up
        for (int i = 0; i < 3; i++) holdStick(c, aim, 0.0f, -1.0f);
        CHECK_NEAR(fromHead(c->screenPose), GRAB_PUSH_NEAR_M, 1e-3);
        XrVector2f north = { 0.0f, 1.0f };
        xrGrabStepStick(c, aim, 0, 1, north);
        CHECK(fromHead(c->screenPose) > GRAB_PUSH_NEAR_M + 0.01f);
        // and the same at the far end
        for (int i = 0; i < 6; i++) holdStick(c, aim, 0.0f, 1.0f);
        CHECK_NEAR(fromHead(c->screenPose), GRAB_PUSH_FAR_M, 1e-3);
        XrVector2f south = { 0.0f, -1.0f };
        xrGrabStepStick(c, aim, 0, 1, south);
        CHECK(fromHead(c->screenPose) < GRAB_PUSH_FAR_M - 0.01f);

        // The stick seen over the hold is kept for the line said at letting go
        CHECK(c->grabStickLo[HAND_RIGHT] == -1.0f && c->grabStickHi[HAND_RIGHT] == 1.0f);
        CHECK(c->grabStickLo[HAND_LEFT] == 0.0f && c->grabStickHi[HAND_LEFT] == 0.0f);

        // Let go, it stays where the stick left it and is saved
        XrPosef left = c->screenPose;
        xrGrabStepAt(c, aim, 0, 0, 0.0f);
        CHECK(c->grabMode == GRAB_NONE && c->poseDirty);
        CHECK(dist(pos(c->screenPose), pos(left)) == 0.0f);
        c->poseDirty = 0;
    }

    // The keyboard held by its bar takes the same push and pull
    pictureAt3m(s, 0.0f);
    c->kbOpen = 1;
    c->kbMoved = 0;
    kbPlace(c);
    XrPosef aim = aimAt(HAND, pillPoint(c));
    CHECK(xrGrabStepAt(c, aim, 0, 1, 0.0f) == HOVER_KBBAR);
    CHECK(c->grabMode == GRAB_KBMOVE);
    xrGrabStepAt(c, aim, 0, 1, 0.0f);
    float start = fromHead(c->kbPose);
    holdStick(c, aim, 0.0f, 1.0f);
    float out = fromHead(c->kbPose) - start;
    CHECK_NEAR(out, second, 0.01);
    holdStick(c, aim, 0.0f, -1.0f);
    CHECK_NEAR(fromHead(c->kbPose), start, 1e-3);
    holdStick(c, aim, 0.3f, -0.8f);
    CHECK_NEAR(start - fromHead(c->kbPose), second, 0.01);
    xrGrabStepAt(c, aim, 0, 0, 0.0f);
    CHECK(c->grabMode == GRAB_NONE);
    c->kbOpen = 0;
    c->kbMoved = 0;

    c->screenPose = keepPose;
    c->screenWidth = keepW;
    c->screenRadius = keepR;
    c->panelCurve = keepCurve;
}

// Native (lane beam-streak): where the beam is in the frame's list of layers, -1 for nowhere
static int beamIndex(const Scene* s) {
    UiRectPx b = xrAtlasRect(ATLAS_BEAM);
    for (uint32_t i = 0; i < s->out.count; i++) {
        const XrCompositionLayerQuad* q = (const XrCompositionLayerQuad*)s->out.order[i];
        if (q->type == XR_TYPE_COMPOSITION_LAYER_QUAD && q->subImage.imageRect.offset.x == b.x
                && q->subImage.imageRect.offset.y == b.y && q->subImage.imageRect.extent.width == b.w) {
            return (int)i;
        }
    }
    return -1;
}

// Native (lane beam-streak): one hover state's frame, checked against the first: the same number of
// layers and the beam in the same place in the list
static void sameBeamPlace(Scene* s, int* index, uint32_t* count, const char* what) {
    scene_frame(s, NULL);
    int at = beamIndex(s);
    CHECK(at > 0);
    if (*index < 0) {
        *index = at;
        *count = s->out.count;
        printf("  %s: %u layers, the beam %d\n", what, s->out.count, at);
        return;
    }
    if (at != *index || s->out.count != *count) {
        printf("  %s: %u layers, the beam %d (was %u, %d)\n", what, s->out.count, at, *count, *index);
    }
    CHECK(at == *index);
    CHECK(s->out.count == *count);
    CHECK(s->out.count <= (uint32_t)s->ctx->maxLayerCount);
}

// Native (lane beam-streak): every held place is clear and a millimetre across
static void heldPlacesClear(Scene* s) {
    const uint8_t* px = xrAtlasPixels(s->ctx);
    UiRectPx o = xrAtlasRect(ATLAS_OUTLINE);
    for (uint32_t i = 0; i < s->out.count; i++) {
        const XrCompositionLayerQuad* q = (const XrCompositionLayerQuad*)s->out.order[i];
        if (q->type != XR_TYPE_COMPOSITION_LAYER_QUAD || q->size.width > 0.0011f
                || q->subImage.imageRect.offset.x != o.x + o.w / 2 - 2) {
            continue;
        }
        const XrRect2Di* r = &q->subImage.imageRect;
        unsigned alpha = 0;
        for (int y = r->offset.y; y < r->offset.y + r->extent.height; y++) {
            for (int x = r->offset.x; x < r->offset.x + r->extent.width; x++) {
                alpha += px[((size_t)y * UI_ATLAS_W + (size_t)x) * 4 + 3];
            }
        }
        CHECK(alpha == 0);
    }
}

// Native (lane beam-streak): the ray sweeping over the keys and the gaps between them, the bar and
// its buttons, the corners, the halo, the sheets' buttons and off the picture leaves the beam where it
// was in the list of layers. Anything ahead of it that came and went with the hover moved it a place
// up or down, and the frame the key ring went (2026-10-31) was the one frame SteamVR drew the beam as
// a white streak across the view.
static void hoverKeepsBeamPlace(Scene* s) {
    XrCtx* c = s->ctx;
    static const int furniture[] = { HOVER_NONE, HOVER_SCREEN, HOVER_BAR, HOVER_CORNER, HOVER_HALO,
                                     HOVER_ENVBUTTON, HOVER_COGBUTTON, HOVER_KBBUTTON, HOVER_EXITBUTTON,
                                     HOVER_STEREOBUTTON, HOVER_RAYBUTTON, HOVER_AIMBUTTON,
                                     HOVER_PADBUTTON, HOVER_HOSTBUTTON, HOVER_HEREBUTTON, HOVER_STATS,
                                     HOVER_SCREEN, HOVER_NONE };
    for (int locked = 0; locked < 2; locked++) {
        for (int stats = 0; stats < 2; stats++) {
            scene_reset(s);
            s->settings.head_locked = locked;
            c->headLockedPref = locked;
            c->overlayVisible = stats;
            Vec3 at = { 0.3f, -0.4f, -2.0f };
            scene_ray_to(s, HAND, at);
            int index = -1;
            uint32_t count = 0;
            printf("the picture, %s, stats %s\n", locked ? "head locked" : "world locked", stats ? "on" : "off");
            for (size_t k = 0; k < sizeof(furniture) / sizeof(furniture[0]); k++) {
                c->hoverKind = furniture[k];
                c->hoverCorner = (int)k % 4;
                sameBeamPlace(s, &index, &count, "the ray over the picture's furniture");
                heldPlacesClear(s);
            }
        }
    }
    s->settings.head_locked = 0;
    c->headLockedPref = 0;
    c->overlayVisible = 0;

    // The keyboard: keys, the gaps between them, its bar, the picture's corners, off everything
    defaults(s);
    Vec3 key = onPlane(c->kbPose, c->kbW, c->kbH, 0.5f, 0.5f);
    scene_ray_to(s, HAND, key);
    int index = -1;
    uint32_t count = 0;
    printf("the keyboard\n");
    static const int kbHovers[] = { HOVER_KBPANEL, HOVER_KBPANEL, HOVER_KBPANEL, HOVER_KBBAR, HOVER_CORNER,
                                    HOVER_SCREEN, HOVER_NONE, HOVER_KBPANEL };
    static const int kbKeys[] = { 3, -1, 5, -1, -1, -1, -1, KB_KEY_LAYOUT };
    for (size_t k = 0; k < sizeof(kbHovers) / sizeof(kbHovers[0]); k++) {
        c->hoverKind = kbHovers[k];
        c->kbHoverKey = kbKeys[k];
        sameBeamPlace(s, &index, &count, "the ray over the keyboard");
        heldPlacesClear(s);
    }
    c->kbOpen = 0;
    c->kbHoverKey = -1;

    // The room grid: its cells and off them
    scene_reset(s);
    scene_ray_to(s, HAND, key);
    c->pickerOpen = 1;
    c->panelFades[FADE_PICKER].level = 1.0f;
    index = -1;
    printf("the room grid\n");
    for (int cell = -1; cell < 3; cell++) {
        c->pickerHover = cell;
        c->hoverKind = cell < 0 ? HOVER_NONE : HOVER_PICKER;
        sameBeamPlace(s, &index, &count, "the ray over the room grid");
    }
    scene_reset(s);
}

// Native (lane eye-calibration): the calibration's run coming up from the Accessibility tab and going
// down again, at each dot and in both its phases, head locked and world locked, with a controller's
// ray on the panel and with the head pointing: the frame's count and the beam's place in the list
// (held while the run is up) never change, the run's dot and label are drawn, and every held place
// is clear
static int findArt(const Scene* s, int slot, float minSize) {
    UiRectPx r = xrAtlasRect(slot);
    for (uint32_t i = 0; i < s->out.count; i++) {
        const XrCompositionLayerQuad* q = (const XrCompositionLayerQuad*)s->out.order[i];
        if (q->type == XR_TYPE_COMPOSITION_LAYER_QUAD && q->subImage.imageRect.offset.x == r.x
                && q->subImage.imageRect.offset.y == r.y && q->subImage.imageRect.extent.width == r.w
                && q->size.width > minSize) {
            return (int)i;
        }
    }
    return -1;
}

static void eyeCalKeepsBeamPlace(Scene* s) {
    XrCtx* c = s->ctx;
    for (int locked = 0; locked < 2; locked++) {
        for (int headPoints = 0; headPoints < 2; headPoints++) {
            scene_reset(s);
            s->settings.head_locked = locked;
            c->headLockedPref = locked;
            xrSetPointerSource(c, headPoints ? POINTER_SOURCE_HEAD : POINTER_SOURCE_CONTROLLER, 0);
            c->cogOpen = 1;
            c->cogTab = COG_TAB_ACCESS;
            c->panelFades[FADE_COG].level = 1.0f;
            c->cogPose = cogPanelPose(c, &c->cogW, &c->cogH);
            Vec3 start = onPlane(c->cogPose, c->cogW, c->cogH, 0.6f, cogRowV(COG_TAB_ACCESS, COG_ACCESS_ROW_EYECAL));
            Vec3 eye = { 0, 0, 0 };
            scene_ray_to(s, headPoints ? eye : HAND, start);
            if (headPoints) {
                c->beamStart = c->beamEnd;   // the head's pointer is the dot alone
            }
            c->hoverKind = HOVER_COGPANEL;
            printf("the eye calibration, %s, %s pointing\n", locked ? "head locked" : "world locked",
                   headPoints ? "the head" : "a controller");
            scene_frame(s, NULL);
            uint32_t count = s->out.count;
            int at = beamIndex(s);
            const void* beam = at >= 0 ? (const void*)s->out.order[at] : NULL;
            CHECK(headPoints ? at < 0 : at > 0);
            CHECK(findArt(s, UI_PANEL_COG, 0.0f) >= 0);
            printf("  the panel: %u layers, the beam %d\n", count, at);
            xrEyeCalStart(c);
            for (int step = 0; step < EYECAL_DOTS; step++) {
                for (int phase = EYECAL_PHASE_SETTLE; phase <= EYECAL_PHASE_COLLECT; phase++) {
                    c->eyeCalRun.step = step;
                    c->eyeCalRun.phase = phase;
                    c->eyeCalRun.phaseNs = nowNs();
                    scene_frame(s, NULL);
                    if (s->out.count != count) {
                        printf("  dot %d, phase %d: %u layers (was %u)\n", step, phase, s->out.count, count);
                    }
                    CHECK(s->out.count == count);
                    CHECK(s->out.count <= (uint32_t)c->maxLayerCount && s->out.count <= 16);
                    if (beam != NULL) {
                        CHECK(s->out.order[at] == beam && beamIndex(s) < 0);
                    }
                    // the run's dot (the pointer's own is held) and its label, the panel's art moved
                    int dot = findArt(s, ATLAS_DOT, 0.005f), label = findArt(s, UI_PANEL_COG, 0.0f);
                    CHECK(dot >= 0 && label >= 0 && label < dot && (at < 0 || dot < at));
                    heldPlacesClear(s);
                }
            }
            xrEyeCalCancel(c, "the test");
            scene_frame(s, NULL);
            CHECK(s->out.count == count && beamIndex(s) == at);
        }
    }
    xrSetPointerSource(c, POINTER_SOURCE_CONTROLLER, 0);
    s->settings.head_locked = 0;
    c->headLockedPref = 0;
    scene_reset(s);
}

int main(void) {
    Scene* s = scene_new();
    defaults(s);
    grabAndMove(s);
    resets(s);
    layers(s);
    liveSliders(s);
    pictureBarPushPull(s);
    hoverKeepsBeamPlace(s);
    eyeCalKeepsBeamPlace(s);
    scene_free(s);
    return checksDone("m4 kbbar");
}
