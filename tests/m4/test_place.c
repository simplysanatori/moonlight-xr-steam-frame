// Moonlight XR Native, tests/m4: the Screen tab's presets and "bring here",
// Follow mode, the angular-size lock and the layout kept per PC and app
// (xr_place.c, the Screen tab's cells in xr_ui.c, xr_prefs.c's store). The
// context is the headset-free scene's, with the head pose handed to it the
// way the session's locate would. GPLv3.
#include <string.h>

#include "check.h"
#include "scene.h"
#include "xr_depth.h"

static float rad(float deg) { return deg * (float)M_PI / 180.0f; }

static XrQuaternionf yawPitch(float yawDeg, float pitchDeg) {
    Vec3 up = { 0.0f, 1.0f, 0.0f };
    Vec3 right = { 1.0f, 0.0f, 0.0f };
    return quatNorm(quatMul(axisAngleQuat(up, rad(yawDeg)), axisAngleQuat(right, rad(pitchDeg))));
}

static float distFromSeat(const XrCtx* c) {
    XrVector3f p = c->screenPose.position;
    return sqrtf(p.x * p.x + p.y * p.y + p.z * p.z);
}

static void setHead(XrCtx* c, float yawDeg, float pitchDeg) {
    memset(&c->headPose, 0, sizeof(c->headPose));
    c->headPose.orientation = yawPitch(yawDeg, pitchDeg);
    c->headPoseValid = 1;
}

static float outSetting(const float* out) { return out[IN_SETTING]; }

static void freshOut(float* out) {
    memset(out, 0, sizeof(float) * IN_SLOTS);
    out[IN_SETTING] = -1.0f;
}

// The Screen tab's three rows of cells are cells, with the counts the panel draws
static void rowsAreCells(void) {
    for (int row = 0; row < COG_SCREEN_TRACK_ROWS; row++) {
        CHECK(cogRowIsTrack(COG_TAB_SCREEN, row));
    }
    for (int row = COG_SCREEN_TRACK_ROWS; row < COG_SCREEN_ROW_COUNT; row++) {
        CHECK(!cogRowIsTrack(COG_TAB_SCREEN, row));
        CHECK(cogRowCells(COG_TAB_SCREEN, row) == ui_cog_row_cells(COG_TAB_SCREEN, row));
        CHECK(cogRowCells(COG_TAB_SCREEN, row) > 0);
    }
    CHECK(cogTabRowCount(COG_TAB_SCREEN) == COG_SCREEN_ROW_COUNT);
    CHECK(cogRowCells(COG_TAB_SCREEN, COG_SCREEN_ROW_PRESET) == 4);
    CHECK(cogRowCells(COG_TAB_SCREEN, COG_SCREEN_ROW_PER_APP) == 3);
    CHECK(cogRowCells(COG_TAB_SCREEN, COG_SCREEN_ROW_LOCK) == 2);
    CHECK(cogOptionCells(COG_OPTION_HEAD_LOCK) == 3);
}

static void presets(void) {
    Scene* s = scene_new();
    XrCtx* c = s->ctx;
    float out[IN_SLOTS];
    // A screen off to the left and up, curved, tipped, as the presets must leave it
    Settings in = s->settings;
    in.screen_distance_cm = 300;
    in.screen_height_cm = 60;
    in.screen_azimuth_deg = 30;
    in.screen_tilt_deg = -10;
    in.screen_curve = 40;
    settings_clamp(&in);
    xrApplySettings(c, &in);
    float tilt0 = screenPitch(c);
    float curve0 = effectiveCurvature(c);
    float az0 = atan2f(-c->screenPose.position.x, -c->screenPose.position.z);
    float elev0 = asinf(c->screenPose.position.y / distFromSeat(c));
    for (int p = 0; p < LAYOUT_PRESETS; p++) {
        LayoutPreset want = layoutPresetOf(p);
        freshOut(out);
        c->poseDirty = 0;
        cogApplyCell(c, COG_TAB_SCREEN, COG_SCREEN_ROW_PRESET, p, out);
        CHECK_NEAR(distFromSeat(c), want.distanceCm / 100.0, 1e-3);
        CHECK_NEAR(c->screenWidth, want.widthCm / 100.0, 1e-3);
        // the way it sits, its tilt and its curve are as they were
        CHECK_NEAR(atan2f(-c->screenPose.position.x, -c->screenPose.position.z), az0, 1e-4);
        CHECK_NEAR(asinf(c->screenPose.position.y / distFromSeat(c)), elev0, 1e-4);
        CHECK_NEAR(screenPitch(c), tilt0, 1e-4);
        CHECK_NEAR(effectiveCurvature(c), curve0, 1e-4);
        // and it is saved with the placement; no setting id of its own
        CHECK(c->poseDirty);
        CHECK(outSetting(out) < 0.0f);
        CHECK(cogCellInForce(c, COG_TAB_SCREEN, COG_SCREEN_ROW_PRESET) == p);
        // the file's side of it: the placement as it would be saved reads back as the same preset
        Settings saved = in;
        float pose[POSE_VALUES] = { c->screenPose.position.x, c->screenPose.position.y, c->screenPose.position.z,
                                    c->screenPose.orientation.x, c->screenPose.orientation.y,
                                    c->screenPose.orientation.z, c->screenPose.orientation.w,
                                    c->screenWidth, c->screenRadius, c->panelCurve };
        xrPoseToSettings(pose, xrCurveSetting(c), &saved);
        CHECK(layoutPresetAt(saved.screen_distance_cm, saved.screen_width_cm) == p);
    }
    // Moved off a preset it is none of them
    cogApplySlider(c, COG_TAB_SCREEN, COG_SLIDER_DISTANCE, cogRunU(0.2f));
    CHECK(cogCellInForce(c, COG_TAB_SCREEN, COG_SCREEN_ROW_PRESET) == -1);
    scene_free(s);
}

static void bringHere(void) {
    Scene* s = scene_new();
    XrCtx* c = s->ctx;
    float out[IN_SLOTS];
    c->headPos = (XrVector3f){ 0, 0, 0 };
    float width = c->screenWidth;
    float dist0 = distFromSeat(c);
    // Nothing to go by until the head has been located
    c->headPoseValid = 0;
    CHECK(xrBringScreenHere(c) == 0);
    setHead(c, 50.0f, 10.0f);

    // From the panel's cell
    freshOut(out);
    c->poseDirty = 0;
    cogApplyCell(c, COG_TAB_SCREEN, COG_SCREEN_ROW_PRESET, COG_SCREEN_CELL_BRING, out);
    Vec3 gaze = layoutGazeDir(c->headPose.orientation);
    CHECK_NEAR(c->screenPose.position.x, gaze.x * dist0, 1e-3);
    CHECK_NEAR(c->screenPose.position.y, gaze.y * dist0, 1e-3);
    CHECK_NEAR(c->screenPose.position.z, gaze.z * dist0, 1e-3);
    CHECK_NEAR(c->screenWidth, width, 1e-6);
    CHECK(c->poseDirty);
    CHECK(outSetting(out) < 0.0f);
    // It faces the head: its forward axis points back at the seat
    Vec3 back = { 0, 0, 1 };
    Vec3 fwd = quatRotate(c->screenPose.orientation, back);
    CHECK_NEAR(atan2f(fwd.x, fwd.z), atan2f(-c->screenPose.position.x, -c->screenPose.position.z), 1e-3);

    // Head locked: the screen is where you look already, nothing moves
    XrPosef was = c->screenPose;
    c->headLockedPref = 1;
    setHead(c, -70.0f, 0.0f);
    CHECK(xrBringScreenHere(c) == 0);
    CHECK(c->screenPose.position.x == was.position.x && c->screenPose.position.z == was.position.z);
    c->headLockedPref = 0;
    // Called the way the binding does, with nothing else set up
    CHECK(xrBringScreenHere(c) == 1);
    CHECK(c->screenPose.position.x > 0.0f);   // 70 degrees to the right
    scene_free(s);
}

static void switchesAndStore(void) {
    Scene* s = scene_new();
    XrCtx* c = s->ctx;
    float out[IN_SLOTS];

    freshOut(out);
    cogApplyCell(c, COG_TAB_SCREEN, COG_SCREEN_ROW_PER_APP, 0, out);
    CHECK((int)out[IN_SETTING] == SETTING_PER_APP && (int)out[IN_SETTING_VALUE] == 0);
    CHECK(cogCellInForce(c, COG_TAB_SCREEN, COG_SCREEN_ROW_PER_APP) == 0);
    freshOut(out);
    cogApplyCell(c, COG_TAB_SCREEN, COG_SCREEN_ROW_PER_APP, 1, out);
    CHECK((int)out[IN_SETTING] == SETTING_PER_APP && (int)out[IN_SETTING_VALUE] == 1);
    CHECK(cogCellInForce(c, COG_TAB_SCREEN, COG_SCREEN_ROW_PER_APP) == 1);
    freshOut(out);
    cogApplyCell(c, COG_TAB_SCREEN, COG_SCREEN_ROW_PER_APP, COG_SCREEN_CELL_FORGET, out);
    CHECK((int)out[IN_SETTING] == SETTING_FORGET_LAYOUT);
    freshOut(out);
    cogApplyCell(c, COG_TAB_SCREEN, COG_SCREEN_ROW_LOCK, 0, out);
    CHECK((int)out[IN_SETTING] == SETTING_ANGULAR_LOCK && (int)out[IN_SETTING_VALUE] == 0);
    CHECK(cogCellInForce(c, COG_TAB_SCREEN, COG_SCREEN_ROW_LOCK) == 0);

    // The Head locked row's third cell
    freshOut(out);
    cogApplyCell(c, COG_TAB_DISPLAY, COG_OPTION_HEAD_LOCK, 2, out);
    CHECK((int)out[IN_SETTING] == SETTING_HEAD_LOCK && (int)out[IN_SETTING_VALUE] == 2);
    CHECK(c->followOn);
    CHECK(cogOptionValue(c, COG_OPTION_HEAD_LOCK, 0) == 2);
    CHECK(cogOptionValue(c, COG_OPTION_HEAD_LOCK, 1) == 1);
    freshOut(out);
    cogApplyCell(c, COG_TAB_DISPLAY, COG_OPTION_HEAD_LOCK, 0, out);
    CHECK(!c->followOn && cogOptionValue(c, COG_OPTION_HEAD_LOCK, 0) == 0);

    // What gets stored
    Settings st;
    settings_defaults(&st);
    CHECK(xrStoreSetting(c, &st, SETTING_HEAD_LOCK, 2) && st.head_follow == 1 && st.head_locked == 0);
    CHECK(settings_option_cell(&st, &(SessionToggles){ 0 }, COG_OPTION_HEAD_LOCK) == 2);
    CHECK(xrStoreSetting(c, &st, SETTING_HEAD_LOCK, 1) && st.head_follow == 0 && st.head_locked == 1);
    CHECK(xrStoreSetting(c, &st, SETTING_HEAD_LOCK, 0) && st.head_follow == 0 && st.head_locked == 0);
    CHECK(xrStoreSetting(c, &st, SETTING_PER_APP, 0) && st.per_app == 0);
    CHECK(xrStoreSetting(c, &st, SETTING_PER_APP, 1) && st.per_app == 1);
    CHECK(xrStoreSetting(c, &st, SETTING_ANGULAR_LOCK, 0) && st.angular_lock == 0);
    CHECK(!xrStoreSetting(c, &st, SETTING_ANGULAR_LOCK, 0));
    // Forgetting with nothing kept changes nothing; with something kept drops it
    c->layoutHash = settings_app_hash("PC", "App");
    CHECK(!xrStoreSetting(c, &st, SETTING_FORGET_LAYOUT, 0));
    AppLayout l;
    settings_layout_snapshot(&st, &l);
    CHECK(settings_layout_store(&st, c->layoutHash, &l));
    CHECK(xrStoreSetting(c, &st, SETTING_FORGET_LAYOUT, 0) && st.layout_count == 0);
    scene_free(s);
}

// The head turns inside the leash and nothing moves; past it the screen comes
// after the gaze in about FOLLOW_EASE_SEC and stays; the screen keeps its
// distance from the head, its size and its tilt
static void follow(void) {
    Scene* s = scene_new();
    XrCtx* c = s->ctx;
    Settings* st = &s->settings;
    st->head_follow = 1;
    st->follow_leash_deg = 25;
    c->headPos = (XrVector3f){ 0, 0, 0 };
    scene_frame(s, NULL);
    CHECK(xrLayoutFrame(c, st, 1000000000LL) == 0);
    CHECK(c->followOn);
    XrPosef start = c->screenPose;
    float dist0 = distFromSeat(c);
    float width = c->screenWidth;
    float tilt = screenPitch(c);
    int64_t now = 1000000000LL;
    int64_t dt = 11111111LL;

    // 20 degrees to the side for two seconds: still
    setHead(c, 20.0f, 0.0f);
    c->poseDirty = 0;
    for (int i = 0; i < 180; i++) {
        now += dt;
        xrLayoutFrame(c, st, now);
    }
    CHECK(c->screenPose.position.x == start.position.x && c->screenPose.position.z == start.position.z);
    CHECK(!c->follow.easing && !c->poseDirty);

    // 60 degrees: it eases, and not all at once
    setHead(c, 60.0f, 0.0f);
    now += dt;
    xrLayoutFrame(c, st, now);
    CHECK(c->follow.easing);
    Vec3 gaze = layoutGazeDir(c->headPose.orientation);
    Vec3 first = { c->screenPose.position.x, c->screenPose.position.y, c->screenPose.position.z };
    first = vecNorm(first);
    CHECK(vecDot(first, gaze) < 0.99f);
    int frames = 1;
    while (c->follow.easing && frames < 200) {
        now += dt;
        xrLayoutFrame(c, st, now);
        frames++;
    }
    CHECK(!c->follow.easing);
    CHECK(frames >= 50 && frames <= 60);   // 0.6 s at 90 Hz
    Vec3 at = { c->screenPose.position.x, c->screenPose.position.y, c->screenPose.position.z };
    CHECK_NEAR(sqrtf(vecDot(at, at)), dist0, 1e-3);
    CHECK_NEAR(vecDot(vecNorm(at), gaze), 1.0, 1e-4);
    CHECK_NEAR(c->screenWidth, width, 1e-6);
    CHECK_NEAR(screenPitch(c), tilt, 1e-4);
    // landed: saved like a moved screen, then still
    CHECK(c->poseDirty);
    XrPosef landed = c->screenPose;
    c->poseDirty = 0;
    for (int i = 0; i < 90; i++) {
        now += dt;
        xrLayoutFrame(c, st, now);
    }
    CHECK(c->screenPose.position.x == landed.position.x && !c->poseDirty);

    // Not while a panel is up, locked to the head, held, or in the launcher
    setHead(c, -60.0f, 0.0f);
    c->cogOpen = 1;
    now += dt;
    xrLayoutFrame(c, st, now);
    CHECK(!c->follow.easing && c->screenPose.position.x == landed.position.x);
    c->cogOpen = 0;
    c->grabMode = GRAB_MOVE;
    now += dt;
    xrLayoutFrame(c, st, now);
    CHECK(!c->follow.easing);
    c->grabMode = GRAB_NONE;
    st->head_locked = 1;
    c->headLockedPref = 1;
    now += dt;
    xrLayoutFrame(c, st, now);
    CHECK(!c->follow.easing && !c->followOn);
    st->head_locked = 0;
    c->headLockedPref = 0;
    // and with Follow off it never starts
    st->head_follow = 0;
    now += dt;
    xrLayoutFrame(c, st, now);
    CHECK(!c->follow.easing && !c->followOn);
    // Follow on again with the head already out: it comes after it
    st->head_follow = 1;
    now += dt;
    xrLayoutFrame(c, st, now);
    CHECK(c->follow.easing);
    scene_free(s);
}

// Pushing or pulling the held screen keeps its size in the view
static void angularLock(void) {
    Scene* s = scene_new();
    XrCtx* c = s->ctx;
    Settings* st = &s->settings;
    c->headPos = (XrVector3f){ 0, 0, 0 };
    scene_frame(s, NULL);
    // a screen 2 m off and 2 m wide, held, as the grab leaves it
    c->screenPose.position = (XrVector3f){ 0, 0, -2.0f };
    c->screenWidth = 2.0f;
    c->screenRadius = 6.0f;
    c->grabMode = GRAB_MOVE;
    c->grabPush = 0.0f;
    xrLayoutFrame(c, st, 1000000000LL);
    CHECK_NEAR(c->screenWidth, 2.0, 1e-6);

    // pushed 2 m further out: the input pass has moved it to 4 m and says so
    c->screenPose.position.z = -4.0f;
    c->grabPush = 2.0f;
    xrLayoutFrame(c, st, 1011111111LL);
    CHECK_NEAR(c->screenWidth, 4.0, 1e-5);
    CHECK_NEAR(c->screenRadius, 12.0, 1e-4);
    // the angle it covers is what it was
    CHECK_NEAR(c->screenWidth / 4.0, 2.0 / 2.0, 1e-5);
    // pulled back to 3 m (a push of 1): the width is the starting width times 3 over 2
    c->screenPose.position.z = -3.0f;
    c->grabPush = 1.0f;
    xrLayoutFrame(c, st, 1022222222LL);
    CHECK_NEAR(c->screenWidth, 3.0, 1e-5);
    // let go: the width stays, and the next hold starts from it
    c->grabMode = GRAB_NONE;
    xrLayoutFrame(c, st, 1033333333LL);
    CHECK_NEAR(c->screenWidth, 3.0, 1e-5);
    CHECK(!c->lockActive);

    // moved by a hand, not pushed: untouched
    c->grabMode = GRAB_MOVE;
    c->grabPush = 0.0f;
    c->screenPose.position.z = -5.0f;
    xrLayoutFrame(c, st, 1044444444LL);
    CHECK_NEAR(c->screenWidth, 3.0, 1e-5);

    // switched off: pushing changes the distance and nothing else
    st->angular_lock = 0;
    c->screenPose.position.z = -6.0f;
    c->grabPush = 1.0f;
    xrLayoutFrame(c, st, 1055555555LL);
    CHECK_NEAR(c->screenWidth, 3.0, 1e-5);
    st->angular_lock = 1;

    // held to the widths a screen may have
    c->grabMode = GRAB_NONE;
    xrLayoutFrame(c, st, 1066666666LL);
    c->grabMode = GRAB_MOVE;
    c->screenWidth = 5.0f;
    c->screenPose.position.z = -7.9f;
    c->grabPush = 6.0f;   // from 1.9 m to 7.9 m: about 4x
    xrLayoutFrame(c, st, 1077777777LL);
    CHECK_NEAR(c->screenWidth, SCREEN_MAX_WIDTH, 1e-5);
    scene_free(s);
}

// Each PC and app keeps its own layout, found again by name
static void perApp(void) {
    Scene* sc = scene_new();
    XrCtx* c = sc->ctx;
    Settings* s = &sc->settings;
    int64_t now = 5000000000LL;
    scene_frame(sc, NULL);

    xrSetStreamIdentity("Desk PC", "Desktop");
    CHECK(xrLayoutFrame(c, s, now) == 0);
    CHECK(c->layoutHash == settings_app_hash("Desk PC", "Desktop"));
    CHECK(s->layout_count == 0);

    // The user lays the screen out for the desktop: the placement lands in the
    // settings (the session does that from the pose), and a moment later it is kept
    s->screen_distance_cm = 120;
    s->screen_width_cm = 160;
    s->stereo = 0;
    now += 100000000LL;
    CHECK(xrLayoutFrame(c, s, now) == 0);
    now += 400000000LL;
    CHECK(xrLayoutFrame(c, s, now) == 0);   // still inside the debounce
    s->screen_height_cm = 20;   // a drag still going moves the end of it on
    now += 500000000LL;
    CHECK(xrLayoutFrame(c, s, now) == 0);
    now += 800000000LL;
    CHECK(xrLayoutFrame(c, s, now) == 1);
    CHECK(s->layout_count == 1);
    const AppLayout* kept = settings_layout_find(s, c->layoutHash);
    CHECK(kept != NULL && kept->distance_cm == 120 && kept->width_cm == 160 && kept->height_cm == 20);
    CHECK(kept->stereo == 0);
    now += 2000000000LL;
    CHECK(xrLayoutFrame(c, s, now) == 0);   // once only

    // Another app starts from what is in force, and is kept on its own
    xrSetStreamIdentity("Desk PC", "Cyberpunk 2077");
    now += 100000000LL;
    CHECK(xrLayoutFrame(c, s, now) == 0);
    CHECK(s->screen_distance_cm == 120);
    s->screen_distance_cm = 400;
    s->screen_width_cm = 500;
    now += 100000000LL;
    xrLayoutFrame(c, s, now);
    now += 900000000LL;
    CHECK(xrLayoutFrame(c, s, now) == 1);
    CHECK(s->layout_count == 2);

    // Back to the first: its layout is put back in the settings and the screen
    xrSetStreamIdentity("Desk PC", "Desktop");
    now += 100000000LL;
    CHECK(xrLayoutFrame(c, s, now) == 1);
    CHECK(s->screen_distance_cm == 120 && s->screen_width_cm == 160 && s->screen_height_cm == 20);
    CHECK_NEAR(c->screenWidth, 1.6, 1e-5);
    CHECK_NEAR(distFromSeat(c), sqrtf(1.2f * 1.2f), 0.05);
    CHECK_NEAR(c->screenPose.position.y, 0.20, 1e-3);
    // it is not kept again for having been put back
    now += 2000000000LL;
    CHECK(xrLayoutFrame(c, s, now) == 0);
    CHECK(s->layout_count == 2);

    // Switching before the debounce is up still keeps the last change
    s->screen_distance_cm = 180;
    now += 100000000LL;
    xrLayoutFrame(c, s, now);
    xrSetStreamIdentity("Desk PC", "Cyberpunk 2077");
    now += 100000000LL;
    CHECK(xrLayoutFrame(c, s, now) == 1);
    CHECK(settings_layout_find(s, settings_app_hash("Desk PC", "Desktop"))->distance_cm == 180);
    CHECK(s->screen_distance_cm == 400);

    // Forget: this app's layout goes, the screen stays where it is, and nothing is kept until it changes
    CHECK(xrStoreSetting(c, s, SETTING_FORGET_LAYOUT, 0));
    CHECK(s->layout_count == 1 && s->screen_distance_cm == 400);
    for (int i = 0; i < 6; i++) {
        now += 500000000LL;
        CHECK(xrLayoutFrame(c, s, now) == 0);
    }
    CHECK(s->layout_count == 1);
    s->screen_distance_cm = 410;
    now += 100000000LL;
    xrLayoutFrame(c, s, now);
    now += 900000000LL;
    CHECK(xrLayoutFrame(c, s, now) == 1 && s->layout_count == 2);

    // Switched off: nothing is kept and nothing is put back
    s->per_app = 0;
    int count = s->layout_count;
    s->screen_distance_cm = 250;
    now += 100000000LL;
    xrLayoutFrame(c, s, now);
    now += 2000000000LL;
    CHECK(xrLayoutFrame(c, s, now) == 0 && s->layout_count == count);
    xrSetStreamIdentity("Desk PC", "Desktop");
    now += 100000000LL;
    CHECK(xrLayoutFrame(c, s, now) == 0 && s->screen_distance_cm == 250);
    s->per_app = 1;

    // No app, nothing to key by
    xrSetStreamIdentity("Desk PC", "");
    now += 100000000LL;
    xrLayoutFrame(c, s, now);
    CHECK(c->layoutHash == 0);
    s->screen_distance_cm = 260;
    now += 2000000000LL;
    xrLayoutFrame(c, s, now);
    now += 2000000000LL;
    CHECK(xrLayoutFrame(c, s, now) == 0 && s->layout_count == count);
    scene_free(sc);
}

// PLAN 4b.6: the zoom and the pan are part of the layout kept per PC and app
static void zoomMemory(void) {
    Scene* sc = scene_new();
    XrCtx* c = sc->ctx;
    Settings* s = &sc->settings;
    int64_t now = 5000000000LL;
    scene_frame(sc, NULL);
    CHECK(c->zoomPct == 100 && c->zoomX == 50 && c->zoomY == 50);

    xrSetStreamIdentity("Desk PC", "Desktop");
    CHECK(xrLayoutFrame(c, s, now) == 0);
    // Zoom in on the top right corner of the desktop, three presses aimed at it
    c->zoomHitValid = 1;
    c->zoomHitU = 1.0f;
    c->zoomHitV = 0.0f;
    for (int i = 0; i < 4; i++) CHECK(xrZoomStep(c, 1) == 1);
    CHECK(c->zoomPct == 200 && c->zoomX == 100 && c->zoomY == 0);
    // The settings take it on the next frame (and want saving), then it is kept after the debounce
    now += 100000000LL;
    CHECK(xrLayoutFrame(c, s, now) == 1);
    CHECK(s->zoom == 200 && s->zoom_x == 100 && s->zoom_y == 0);
    now += 100000000LL;
    CHECK(xrLayoutFrame(c, s, now) == 0);
    now += 900000000LL;
    CHECK(xrLayoutFrame(c, s, now) == 1 && s->layout_count == 1);
    const AppLayout* kept = settings_layout_find(s, c->layoutHash);
    CHECK(kept != NULL && kept->zoom == 200 && kept->zoom_x == 100 && kept->zoom_y == 0);
    // The pointer at the screen's corner is the desktop's corner, and the middle is the middle of that corner part
    float u = 1.0f, v = 0.0f;
    xrDepthPointerUv(c, &u, &v);
    CHECK_NEAR(u, 1.0, 1e-5);
    CHECK_NEAR(v, 0.0, 1e-5);
    u = 0.5f;
    v = 0.5f;
    xrDepthPointerUv(c, &u, &v);
    CHECK_NEAR(u, 0.75, 1e-5);
    CHECK_NEAR(v, 0.25, 1e-5);
    // The file has it, and reads back the same
    char* text = settings_to_text(s);
    CHECK(strstr(text, "zoom = 200") != NULL && strstr(text, "zoom_x = 100") != NULL);
    Settings back;
    settings_defaults(&back);
    settings_from_text(&back, text, NULL);
    free(text);
    const AppLayout* again = settings_layout_find(&back, c->layoutHash);
    CHECK(again != NULL && again->zoom == 200 && again->zoom_x == 100 && again->zoom_y == 0);
    CHECK(back.zoom == 200 && back.zoom_x == 100 && back.zoom_y == 0);

    // Another app: it starts from what is in force; zoomed back out it is kept as the whole picture
    xrSetStreamIdentity("Desk PC", "Video Player");
    now += 100000000LL;
    xrLayoutFrame(c, s, now);
    CHECK(c->zoomPct == 200);
    CHECK(xrZoomStep(c, 0) == 1);
    now += 100000000LL;
    xrLayoutFrame(c, s, now);
    now += 900000000LL;
    CHECK(xrLayoutFrame(c, s, now) == 1 && s->layout_count == 2);
    CHECK(settings_layout_find(s, c->layoutHash)->zoom == 100);
    CHECK(c->zoomPct == 100 && s->zoom == 100);

    // Back to the desktop: its zoom and place are put back in the settings and the context, and the picture
    xrSetStreamIdentity("Desk PC", "Desktop");
    now += 100000000LL;
    CHECK(xrLayoutFrame(c, s, now) == 1);
    CHECK(c->zoomPct == 200 && c->zoomX == 100 && c->zoomY == 0);
    CHECK(s->zoom == 200 && s->zoom_x == 100 && s->zoom_y == 0);
    now += 2000000000LL;
    CHECK(xrLayoutFrame(c, s, now) == 0 && s->layout_count == 2);   // not kept again for having been put back

    // Quitting right after a press still keeps it: the next app's identity writes what was pending
    CHECK(xrZoomStep(c, 1) == 1 && c->zoomPct == 250);
    now += 100000000LL;
    xrLayoutFrame(c, s, now);
    xrSetStreamIdentity("Desk PC", "Video Player");
    now += 100000000LL;
    xrLayoutFrame(c, s, now);
    CHECK(settings_layout_find(s, settings_app_hash("Desk PC", "Desktop"))->zoom == 250);
    CHECK(c->zoomPct == 100);

    // Forget drops the app's entry and the zoom stays in force; per app off keeps nothing and puts nothing back
    CHECK(xrStoreSetting(c, s, SETTING_FORGET_LAYOUT, 0));
    CHECK(s->layout_count == 1 && c->zoomPct == 100);
    s->per_app = 0;
    xrSetStreamIdentity("Desk PC", "Desktop");
    now += 100000000LL;
    xrLayoutFrame(c, s, now);
    CHECK(c->zoomPct == 100);
    xrZoomStep(c, 1);
    now += 2000000000LL;
    xrLayoutFrame(c, s, now);
    now += 2000000000LL;
    CHECK(xrLayoutFrame(c, s, now) == 0 && s->layout_count == 1 && s->zoom == 125);
    s->per_app = 1;

    // The settings' limits: the file's values are held to range
    Settings wild;
    settings_defaults(&wild);
    settings_from_text(&wild, "[display]\nzoom = 900\nzoom_x = -5\nzoom_y = 300\n", NULL);
    CHECK(wild.zoom == 400 && wild.zoom_x == 0 && wild.zoom_y == 100);
    xrApplySettings(c, &wild);
    CHECK(c->zoomPct == 400 && c->zoomX == 0 && c->zoomY == 100);
    Settings out = wild;
    out.zoom = 100;
    xrSettingsFromCtx(c, &out);
    CHECK(out.zoom == 400 && out.zoom_x == 0 && out.zoom_y == 100);
    scene_free(sc);
}

int main(void) {
    rowsAreCells();
    presets();
    bringHere();
    switchesAndStore();
    follow();
    angularLock();
    perApp();
    zoomMemory();
    return checksDone("m4 place");
}
