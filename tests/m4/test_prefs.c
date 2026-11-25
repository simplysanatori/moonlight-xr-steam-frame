// Moonlight XR Native, tests/m4: config.ini <-> XrCtx (xr_prefs.c), the way
// upstream's nativeInit arguments and JNI setters fed the renderer and
// XrRenderer.applySetting / saveScreenPose stored what it handed back.
// GPLv3.
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "scene.h"
#include "gamepads.h"
#include "rumble.h"

static float frand(float lo, float hi) { return lo + (hi - lo) * (float)rand() / (float)RAND_MAX; }

static void roundTripDefaults(void) {
    Scene* s = scene_new();
    Settings back = s->settings;
    xrSettingsFromCtx(s->ctx, &back);
    CHECK(settings_equal(&back, &s->settings));
    scene_free(s);
}

// Every setting the context owns, away from its default, survives
// config.ini -> context -> config.ini
static void roundTripChanged(void) {
    Settings in;
    settings_defaults(&in);
    in.sharpening = 1;
    in.supersampling = 2;
    in.picture_filter = 0;
    in.chroma_from_luma = 0;
    in.stats = 1;
    in.glow = 1;
    in.glow_level = 35;
    in.screen_light = 0;
    in.pointer_sleep = 0;
    in.controller_model = 1;
    in.click_sound = 0;
    in.pad_grip = PAD_GRIP_NONE;
    in.click_steadiness = CLICK_STEADY_STEADY;
    in.scroll_speed = 175;
    in.rumble_mix = PAD_RUMBLE_MIX_SPLIT;
    in.rumble_intensity = 40;
    in.pointer_dot = 0;
    in.head_aim_sensitivity = HEAD_AIM_SENSITIVITY_MAX;
    in.head_aim_deadzone = HEAD_AIM_DEADZONE_MIN;
    in.picture_brightness = 12;
    in.picture_contrast = 120;
    in.picture_gamma = 90;
    in.picture_saturation = 140;
    in.bg_dim = 45;
    in.glow_spread = 70;
    in.eye_balance = -30;
    settings_set_str(&in, settings_find("void_color"), "#336699");
    in.screen_distance_cm = 250;
    in.screen_height_cm = 30;
    in.screen_azimuth_deg = 20;
    in.screen_tilt_deg = -10;
    in.screen_rotate_deg = 15;
    in.screen_curve = 40;
    in.screen_width_cm = 400;
    in.room[1].brightness = 80;
    settings_clamp(&in);
    XrCtx* ctx = calloc(1, sizeof(XrCtx));
    xrCtxDefaults(ctx, 2560, 1440, 90);
    // The launcher's stick dead zone slider (--deadzone) is the Frame controllers' too: untouched
    // (0) or negative it is the default 7 percent, a positive one is itself
    gamepadsSetDeadzone(0);
    xrApplySettings(ctx, &in);
    CHECK_NEAR(ctx->padDeadzone, 0.07, 1e-5);
    gamepadsSetDeadzone(-12);
    xrApplySettings(ctx, &in);
    CHECK_NEAR(ctx->padDeadzone, 0.07, 1e-5);
    gamepadsSetDeadzone(13);
    xrApplySettings(ctx, &in);
    CHECK_NEAR(ctx->padDeadzone, 0.13, 1e-5);
    gamepadsSetDeadzone(0);
    CHECK(ctx->padGripMode == PAD_GRIP_NONE);
    // The rumble settings reached input/rumble: split, and at 40 percent
    {
        RumbleRequest rq[2];
        const int both[2] = { 1, 1 };
        rumbleHostCallback(0, 65535, 0);
        rumblePoll(rumbleGlobal(), both, 1000000, rq);
        CHECK(rq[0].action == PAD_RUMBLE_APPLY && rq[1].action == PAD_RUMBLE_KEEP);
        CHECK_NEAR(rq[0].amplitude, 0.4, 1e-6);
        rumbleStop(rumbleGlobal());
        rumblePoll(rumbleGlobal(), both, 2000000, rq);
        rumbleSetMix(rumbleGlobal(), PAD_RUMBLE_MIX_SPLIT, 100);
    }
    // what the context took
    CHECK(ctx->sharpenMode == 1 && ctx->supersampleMode == 2 && ctx->overlayVisible == 1);
    CHECK(ctx->dotOn == 0 && ctx->pointerSleepOn == 0 && ctx->modelOn == 1 && ctx->clickSoundOn == 0);
    CHECK(ctx->gradeOn == 1);
    CHECK(ctx->bgDim == 45 && ctx->glowSpread == 70);
    CHECK_NEAR(ctx->eyeBalance, -0.30, 1e-6);
    CHECK_NEAR(ctx->voidColor[0], 0.2, 1e-6);
    CHECK_NEAR(ctx->voidColor[1], 0.4, 1e-6);
    CHECK_NEAR(ctx->voidColor[2], 0.6, 1e-6);
    CHECK_NEAR(ctx->ambiIntensity, 0.35, 1e-5);
    CHECK_NEAR(effectiveCurvature(ctx), 0.40, 1e-5);
    CHECK_NEAR(ctx->screenWidth, 4.0, 1e-5);
    CHECK_NEAR(sqrtf(ctx->screenPose.position.x * ctx->screenPose.position.x
                     + ctx->screenPose.position.y * ctx->screenPose.position.y
                     + ctx->screenPose.position.z * ctx->screenPose.position.z), 2.5, 1e-4);
    CHECK_NEAR(ctx->screenPose.position.y, 0.30, 1e-5);
    CHECK(ctx->screenPose.position.x < 0.0f);   // azimuth: left of straight ahead
    CHECK_NEAR(screenPitch(ctx) * 180 / M_PI, -10.0, 0.05);
    CHECK_NEAR(-screenRoll(ctx) * 180 / M_PI, 15.0, 0.05);
    Settings back = in;
    settings_defaults(&back);
    back.environment = in.environment;
    xrSettingsFromCtx(ctx, &back);
    CHECK(back.sharpening == 1 && back.supersampling == 2 && back.stats == 1 && back.glow == 1);
    CHECK(back.picture_filter == 0 && back.chroma_from_luma == 0);
    CHECK(back.glow_level == 35 && back.screen_light == 0 && back.pointer_sleep == 0);
    CHECK(back.controller_model == 1 && back.click_sound == 0 && back.pointer_dot == 0);
    CHECK(back.head_aim_sensitivity == in.head_aim_sensitivity && back.head_aim_deadzone == in.head_aim_deadzone);
    CHECK(back.picture_brightness == 12 && back.picture_contrast == 120 && back.picture_gamma == 90
          && back.picture_saturation == 140);
    CHECK(back.screen_distance_cm == 250 && back.screen_height_cm == 30 && back.screen_azimuth_deg == 20);
    CHECK(back.screen_tilt_deg == -10 && back.screen_rotate_deg == 15 && back.screen_curve == 40
          && back.screen_width_cm == 400);
    CHECK(back.room[1].brightness == 80);
    CHECK(back.bg_dim == 45 && back.glow_spread == 70);
    free(ctx);
}

// Native (lane wave3-visual): the Room tab's Background dim and (issue #7) Glow spread rows
// through the panel's own code (drag, step, reset) and config.ini, and what the
// backdrop behind the picture comes to
static void surroundingsRows(void) {
    Scene* sc = scene_new();
    XrCtx* c = sc->ctx;
    float out[IN_SLOTS];
    CHECK(cogTabRowCount(COG_TAB_ROOM) == COG_ROOM_ROW_COUNT);
    CHECK(cogRowIsTrack(COG_TAB_ROOM, COG_ROOM_ROW_BG_DIM) && cogRowIsTrack(COG_TAB_ROOM, COG_ROOM_ROW_GLOW_SPREAD));
    CHECK(c->bgDim == 0 && c->glowSpread == GLOW_SPREAD_DEFAULT && c->eyeBalance == 0.0f);

    // Background: whole five percent steps, the thumb and the file agree
    for (int step = 0; step <= 20; step++) {
        cogApplySlider(c, COG_TAB_ROOM, COG_ROOM_ROW_BG_DIM, cogRunU(step / 20.0f));
        CHECK(c->bgDim == step * 5);
        CHECK_NEAR(cogSliderValue(c, COG_TAB_ROOM, COG_ROOM_ROW_BG_DIM), step / 20.0, 1e-5);
        Settings live = sc->settings;
        xrSettingsFromCtx(c, &live);
        CHECK(live.bg_dim == step * 5);
    }
    // between two steps it lands on the nearer
    cogApplySlider(c, COG_TAB_ROOM, COG_ROOM_ROW_BG_DIM, cogRunU(0.31f));
    CHECK(c->bgDim == 30);
    // letting go hands the value to the file
    memset(out, 0, sizeof(out));
    out[IN_SETTING] = -1;
    c->cogDragSlider = COG_ROOM_ROW_BG_DIM;
    c->cogDragFace = COG_TAB_ROOM;
    cogDragEnded(c, out);
    CHECK((int)out[IN_SETTING] == SETTING_BG_DIM && (int)out[IN_SETTING_VALUE] == 30);
    Settings s;
    settings_defaults(&s);
    CHECK(xrStoreSetting(c, &s, SETTING_BG_DIM, 30) == 1 && s.bg_dim == 30);
    CHECK(xrStoreSetting(c, &s, SETTING_BG_DIM, 30) == 0);
    CHECK(xrStoreSetting(c, &s, SETTING_BG_DIM, 900) == 1 && s.bg_dim == 100);

    // a press on a step button moves it five percent and writes it
    memset(out, 0, sizeof(out));
    out[IN_SETTING] = -1;
    cogStepTrack(c, COG_TAB_ROOM, COG_ROOM_ROW_BG_DIM, 1, out);
    CHECK(c->bgDim == 35 && (int)out[IN_SETTING] == SETTING_BG_DIM && (int)out[IN_SETTING_VALUE] == 35);
    cogStepTrack(c, COG_TAB_ROOM, COG_ROOM_ROW_BG_DIM, -1, out);
    CHECK(c->bgDim == 30);

    // Native (issue #7): Glow spread, five percent steps
    for (int v = GLOW_SPREAD_MIN; v <= GLOW_SPREAD_MAX; v += GLOW_SPREAD_STEP) {
        float t = (float)(v - GLOW_SPREAD_MIN) / (GLOW_SPREAD_MAX - GLOW_SPREAD_MIN);
        cogApplySlider(c, COG_TAB_ROOM, COG_ROOM_ROW_GLOW_SPREAD, cogRunU(t));
        CHECK(c->glowSpread == v);
        CHECK_NEAR(cogSliderValue(c, COG_TAB_ROOM, COG_ROOM_ROW_GLOW_SPREAD), t, 1e-5);
    }
    memset(out, 0, sizeof(out));
    out[IN_SETTING] = -1;
    cogStepTrack(c, COG_TAB_ROOM, COG_ROOM_ROW_GLOW_SPREAD, -1, out);
    CHECK(c->glowSpread == 95 && (int)out[IN_SETTING] == SETTING_GLOW_SPREAD && (int)out[IN_SETTING_VALUE] == 95);
    CHECK(xrStoreSetting(c, &s, SETTING_GLOW_SPREAD, 95) == 1 && s.glow_spread == 95);
    // the glow's image is redrawn when the spread moves
    c->ambilightOn = 1;
    c->glowDrawnOn = 1;
    c->glowDrawnLevel = c->ambiIntensity;
    c->glowDrawnProjected = xrPictureProjected(c) != 0;
    backlightShapeFor((float)c->videoWidth, (float)c->videoHeight, c->glowSpread, 0, &c->glowDrawnShape);
    CHECK(glowStale(c) == 0);
    c->glowSpread = 60;
    CHECK(glowStale(c) == 1);

    // The Room tab's Reset takes both back, with the glow's level, and leaves the grade; the Display
    // tab's takes the grade back and leaves them
    c->ambiIntensity = 0.8f;
    c->bgDim = 60;
    c->pictureUnits[0] = 20;
    surroundingsReset(c);
    CHECK(c->bgDim == BG_DIM_DEFAULT && c->glowSpread == GLOW_SPREAD_DEFAULT);
    CHECK_NEAR(c->ambiIntensity, GLOW_LEVEL_DEFAULT / 100.0, 1e-6);
    CHECK(c->pictureUnits[0] == 20);
    c->bgDim = 60;
    pictureReset(c);
    CHECK(c->bgDim == 60 && c->pictureUnits[0] == 0);
    c->bgDim = 0;
    s.bg_dim = 60;
    s.glow_spread = 85;
    s.glow_level = 80;
    s.picture_contrast = 130;
    CHECK(xrStoreSetting(c, &s, SETTING_RESET_PICTURE, 0) == 1);
    CHECK(s.picture_contrast == 100 && s.bg_dim == 60 && s.glow_spread == 85 && s.glow_level == 80);
    s.picture_contrast = 130;
    CHECK(xrStoreSetting(c, &s, SETTING_RESET_ROOM, 0) == 1);
    CHECK(s.bg_dim == 0 && s.glow_spread == GLOW_SPREAD_DEFAULT && s.glow_level == GLOW_LEVEL_DEFAULT && s.picture_contrast == 130);
    CHECK(xrStoreSetting(c, &s, SETTING_RESET_ROOM, 0) == 0);

    // Not rows past the end
    cogApplySlider(c, COG_TAB_ROOM, COG_ROOM_ROW_COUNT, cogRunU(1.0f));
    CHECK(c->bgDim == 0 && c->glowSpread == GLOW_SPREAD_DEFAULT);

    // The backdrop: a dimmed passthrough's black, a coloured void's colour
    Settings in;
    settings_defaults(&in);
    in.bg_dim = 40;
    settings_set_str(&in, settings_find("void_color"), "#ff0000");
    xrApplySettings(c, &in);
    float bd[4];
    c->passthrough = 1;
    CHECK(xrBackdropColor(c, 1, bd) == 1 && bd[0] == 0.0f && bd[3] > 0.39f && bd[3] < 0.41f);
    c->passthrough = 0;
    c->roomOverride = -1;
    CHECK(roomEffective(c) <= 0);
    CHECK(xrBackdropColor(c, 0, bd) == 1);
    CHECK_NEAR(bd[0], 0.6, 1e-6);
    CHECK(bd[1] == 0.0f && bd[2] == 0.0f && bd[3] == 1.0f);
    in.bg_dim = 100;
    xrApplySettings(c, &in);
    CHECK(xrBackdropColor(c, 0, bd) == 0);   // all the way down is black
    settings_defaults(&in);
    xrApplySettings(c, &in);
    CHECK(xrBackdropColor(c, 0, bd) == 0);   // the default black void has no layer
    scene_free(sc);
}

// The placement as config.ini holds it, against random poses built the way
// the Screen tab and a move leave them
static void poseRoundTrips(void) {
    srand(4);
    for (int i = 0; i < 200; i++) {
        Settings s;
        settings_defaults(&s);
        s.screen_distance_cm = (int)frand(40, 790);
        // config.ini holds the Screen tab's range, 2 m either side of eye level
        float hmax = fminf(s.screen_distance_cm * 0.7f, 200.0f);
        s.screen_height_cm = (int)frand(-hmax, hmax);
        s.screen_azimuth_deg = (int)frand(-170, 170);
        s.screen_tilt_deg = (int)frand(-40, 40);
        s.screen_rotate_deg = (int)frand(-89, 89);
        s.screen_curve = (int)frand(0, 100);
        s.screen_width_cm = (int)frand(80, 800);
        float pose[POSE_VALUES];
        xrPoseFromSettings(&s, pose);
        Settings t;
        settings_defaults(&t);
        xrPoseToSettings(pose, pose[9], &t);
        CHECK(abs(t.screen_distance_cm - s.screen_distance_cm) <= 1);
        CHECK(abs(t.screen_height_cm - s.screen_height_cm) <= 1);
        CHECK(abs(t.screen_azimuth_deg - s.screen_azimuth_deg) <= 1);
        CHECK(abs(t.screen_tilt_deg - s.screen_tilt_deg) <= 1);
        CHECK(abs(t.screen_rotate_deg - s.screen_rotate_deg) <= 1);
        CHECK(t.screen_curve == s.screen_curve && t.screen_width_cm == s.screen_width_cm);
        // facing the seat, square to it
        Vec3 back = { 0, 0, 1 };
        XrQuaternionf q = { pose[3], pose[4], pose[5], pose[6] };
        Vec3 fwd = quatRotate(q, back);
        float yaw = atan2f(fwd.x, fwd.z), toSeat = atan2f(-pose[0], -pose[2]);
        float dyaw = fabsf(remainderf(yaw - toSeat, 2.0f * (float)M_PI));
        CHECK(dyaw < 0.01f);
    }
}

// The Screen tab's thumbs: lane U draws them from config.ini's values, the
// input moves them through xr_ui.c's pose sums; the two agree
static void thumbsAgree(void) {
    Scene* s = scene_new();
    XrCtx* c = s->ctx;
    static const int ROWS[] = { COG_SLIDER_DISTANCE, COG_SLIDER_HEIGHT, COG_SLIDER_TILT, COG_SLIDER_ROTATE,
                                COG_SLIDER_SIZE };
    for (int k = 0; k < 5; k++) {
        for (int step = 0; step <= 10; step++) {
            float pu = cogRunU(step / 10.0f);
            cogApplySlider(c, COG_TAB_SCREEN, ROWS[k], pu);
            Settings live = s->settings;
            xrSettingsFromCtx(c, &live);
            float drawn = settings_screen_slider(&live, ROWS[k]);
            float logic = cogSliderValue(c, COG_TAB_SCREEN, ROWS[k]);
            CHECK_NEAR(drawn, logic, 0.01);
        }
    }
    // The colour rows: through pictureSet and config.ini
    for (int row = 0; row < PICTURE_VALUES; row++) {
        cogApplySlider(c, COG_TAB_PICTURE, row, cogRunU(0.75f));
        Settings live = s->settings;
        xrSettingsFromCtx(c, &live);
        CHECK(settings_picture_value(&live, row) == c->pictureUnits[row]);
    }
    CHECK(c->gradeOn);
    scene_free(s);
}

// What a press hands back (IN_SETTING) lands in the right key of config.ini
static void storeSettings(void) {
    Scene* sc = scene_new();
    XrCtx* c = sc->ctx;
    Settings s;
    settings_defaults(&s);
    struct { int id, value; int* field; int want; } T[] = {
        { SETTING_SHARPEN, 0, &s.sharpening, 0 },
        { SETTING_SUPERSAMPLE, 1, &s.supersampling, 1 },
        { SETTING_STATS, 1, &s.stats, 1 },
        { SETTING_AMBILIGHT, 0, &s.glow, 0 },
        { SETTING_ROOM_LIGHT, 0, &s.screen_light, 0 },
        { SETTING_HEAD_LOCK, 1, &s.head_locked, 1 },
        { SETTING_POINTER_SLEEP, 0, &s.pointer_sleep, 0 },
        { SETTING_CLICK_SOUND, 0, &s.click_sound, 0 },
        { SETTING_CONTROLLER_MODEL, 1, &s.controller_model, 1 },
        { SETTING_HEAD_AIM_SENSITIVITY, 20, &s.head_aim_sensitivity, 20 },
        { SETTING_HEAD_AIM_DEADZONE, 5, &s.head_aim_deadzone, 5 },
        { SETTING_AMBI_LEVEL, 45, &s.glow_level, 45 },
        { SETTING_SEPARATION, 7, &s.separation, 7 },
        { SETTING_CONVERGENCE, 60, &s.convergence, 60 },
        { SETTING_PICTURE_BRIGHTNESS, -20, &s.picture_brightness, -20 },
        { SETTING_PICTURE_CONTRAST, 130, &s.picture_contrast, 130 },
        { SETTING_PICTURE_GAMMA, 80, &s.picture_gamma, 80 },
        { SETTING_PICTURE_SATURATION, 0, &s.picture_saturation, 0 },
        { SETTING_MOUSE_MODE, 1, &s.mouse_mode, 1 },
        { SETTING_SCROLL_SPEED, 150, &s.scroll_speed, 150 },
    };
    for (size_t i = 0; i < sizeof(T) / sizeof(T[0]); i++) {
        int changed = xrStoreSetting(c, &s, T[i].id, T[i].value);
        CHECK(*T[i].field == T[i].want);
        (void)changed;
    }
    CHECK(xrStoreSetting(c, &s, SETTING_RESET_PICTURE, 0));
    Settings d;
    settings_defaults(&d);
    CHECK(s.picture_brightness == d.picture_brightness && s.picture_saturation == d.picture_saturation);
    CHECK(xrStoreSetting(c, &s, SETTING_RESET_3D, 0));
    CHECK(s.separation == d.separation && s.convergence == d.convergence);
    // The pointer row: its three cells through the ported option code
    float out[IN_SLOTS];
    for (int cell = 0; cell < 3; cell++) {
        memset(out, 0, sizeof(out));
        out[IN_SETTING] = -1;
        scene_press_option(c, COG_OPTION_RAY, cell, out);
        CHECK((int)out[IN_SETTING] == SETTING_POINTER_DOT);
        CHECK(cogOptionValue(c, COG_OPTION_RAY, 0) == cell);
        xrStoreSetting(c, &s, (int)out[IN_SETTING], (int)out[IN_SETTING_VALUE]);
        CHECK(s.pointer_dot == (cell >= 1));
        CHECK(raySwitchOn(c->raySetting, c->rayFlipped) == (cell == 2));
    }
    scene_free(sc);
}

// Native (lane wave3-tidy): the 3D tab's depth model rows (Edge, Depth
// detail, Depth shape, Motion reset, Eye balance). A drag moves what the model
// reads at once, the panel's view of the settings follows it, letting go
// hands the units to xrStoreSetting for the key they are stored in, and the
// step buttons move one step of their own size. With the PC drawing the pair
// the rows are dead.
static void depthRows(void) {
    struct { int row, id, steps, step, min, max; } T[] = {
        { COG_ROW3D_EDGE, SETTING_EDGE, 4, 25, EDGE_SHARPNESS_MIN, EDGE_SHARPNESS_MAX },
        { COG_ROW3D_DETAIL, SETTING_DEPTH_DETAIL, 20, 10, DEPTH_SCALE_MIN, DEPTH_SCALE_MAX },
        { COG_ROW3D_SHAPE, SETTING_DEPTH_SHAPE, 20, 10, DEPTH_SCALE_MIN, DEPTH_SCALE_MAX },
        { COG_ROW3D_MOTION, SETTING_MOTION_RESET, 10, 5, MOTION_RESET_MIN, MOTION_RESET_MAX },
        { COG_ROW3D_BALANCE, SETTING_EYE_BALANCE, 20, 10, EYE_BALANCE_MIN, EYE_BALANCE_MAX },
    };
    // the ids are the 3D tab's own and distinct from every other native one
    CHECK(SETTING_EDGE > SETTING_STEREO_SOURCE && SETTING_EYE_BALANCE == SETTING_MOTION_RESET + 1);
    for (size_t i = 0; i < sizeof(T) / sizeof(T[0]); i++) {
        Scene* sc = scene_new();
        XrCtx* c = sc->ctx;
        Settings file = sc->settings;
        const SettingDesc* keys[5] = { settings_find("edge_sharpness"), settings_find("depth_scale"),
                                       settings_find("depth_global"), settings_find("motion_reset"),
                                       settings_find("eye_balance") };
        const SettingDesc* key = keys[i];
        CHECK(key != NULL && key->min == T[i].min && key->max == T[i].max);
        CHECK(cogTabRowCount(COG_TAB_3D) == COG_ROW3D_COUNT);
        CHECK(cogRowIsTrack(COG_TAB_3D, T[i].row));
        CHECK(cogTrackSteps(COG_TAB_3D, T[i].row) == T[i].steps);
        CHECK(T[i].steps * T[i].step == T[i].max - T[i].min);
        // dead with the 3D off and with the PC's pair, live with the depth model
        c->stereoMode = DEPTH_MODE_OFF;
        CHECK(!cogRowLive(c, COG_TAB_3D, T[i].row));
        c->stereoMode = DEPTH_MODE_MODEL;
        c->stereoSource = STEREO_SOURCE_SBS;
        CHECK(!cogRowLive(c, COG_TAB_3D, T[i].row));
        c->stereoSource = STEREO_SOURCE_TB;
        CHECK(!cogRowLive(c, COG_TAB_3D, T[i].row));
        c->stereoSource = STEREO_SOURCE_DEPTH;
        CHECK(cogRowLive(c, COG_TAB_3D, T[i].row));
        // where the file leaves it is where the thumb starts
        CHECK(cogSliderValue(c, COG_TAB_3D, T[i].row) >= 0.0f);
        CHECK_NEAR(cogSliderValue(c, COG_TAB_3D, T[i].row), lanePlace(settings_get(&file, key), T[i].min, T[i].max), 1e-5);
        // a drag to each step lands on that step's units everywhere
        for (int step = 0; step <= T[i].steps; step++) {
            int units = T[i].min + step * T[i].step;
            c->cogDragSlider = T[i].row;
            c->cogDragFace = COG_TAB_3D;
            cogApplySlider(c, COG_TAB_3D, T[i].row, cogRunU((float)step / (float)T[i].steps));
            Settings live = file;
            xrSettingsFromCtx(c, &live);
            CHECK(settings_get(&live, key) == units);
            CHECK_NEAR(cogSliderValue(c, COG_TAB_3D, T[i].row), lanePlace(units, T[i].min, T[i].max), 1e-5);
            float out[IN_SLOTS];
            memset(out, 0, sizeof(out));
            out[IN_SETTING] = -1.0f;
            cogDragEnded(c, out);
            CHECK((int)out[IN_SETTING] == T[i].id && (int)out[IN_SETTING_VALUE] == units);
            Settings stored = file;
            xrStoreSetting(c, &stored, (int)out[IN_SETTING], (int)out[IN_SETTING_VALUE]);
            CHECK(settings_get(&stored, key) == units);
            // and starting again from that file gives the same model values
            XrCtx* again = calloc(1, sizeof(XrCtx));
            xrCtxDefaults(again, 2560, 1440, 90);
            xrApplySettings(again, &stored);
            Settings back = stored;
            xrSettingsFromCtx(again, &back);
            CHECK(settings_equal(&back, &stored));
            CHECK_NEAR(cogSliderValue(again, COG_TAB_3D, T[i].row), lanePlace(units, T[i].min, T[i].max), 1e-5);
            free(again);
        }
        // a step button from the middle of the range goes one step either way
        int mid = (T[i].min + T[i].max) / 2;
        for (int dir = -1; dir <= 1; dir += 2) {
            Settings f = file;
            settings_set(&f, key, mid);
            xrApplySettings(c, &f);
            float out[IN_SLOTS];
            memset(out, 0, sizeof(out));
            out[IN_SETTING] = -1.0f;
            cogStepTrack(c, COG_TAB_3D, T[i].row, dir, out);
            CHECK((int)out[IN_SETTING] == T[i].id);
            CHECK((int)out[IN_SETTING_VALUE] == mid + dir * T[i].step);
        }
        scene_free(sc);
    }
    // Edge at 0 is upstream's soft edges, and a step of it moves both knobs
    {
        Scene* sc = scene_new();
        XrCtx* c = sc->ctx;
        c->stereoMode = DEPTH_MODE_MODEL;
        float sigma0 = c->upsampleSigmaR;
        cogApplySlider(c, COG_TAB_3D, COG_ROW3D_EDGE, cogRunU(0.5f));
        CHECK_NEAR(c->depthSharp, 0.5, 1e-6);
        CHECK(c->upsampleSigmaR < sigma0);
        scene_free(sc);
    }
}

// The cog's Ray row is saved in both halves (Off / Dot / Ray -> show_ray and
// pointer_dot); the bar button's ray switch is not
static void rayRowSaved(void) {
    for (int cell = 0; cell < 3; cell++) {
        Scene* sc = scene_new();
        XrCtx* c = sc->ctx;
        Settings s;
        settings_defaults(&s);
        float out[IN_SLOTS];
        memset(out, 0, sizeof(out));
        out[IN_SETTING] = -1;
        scene_press_option(c, COG_OPTION_RAY, cell, out);
        CHECK((int)out[IN_SETTING] == SETTING_POINTER_DOT);
        // a cell that matches the defaults reports no change, the others do
        int changed = xrStoreSetting(c, &s, (int)out[IN_SETTING], (int)out[IN_SETTING_VALUE]);
        CHECK(changed == (cell != 2));
        CHECK(s.show_ray == (cell == 2));
        CHECK(s.pointer_dot == (cell >= 1));

        // through config.ini and into the next session
        char* text = settings_to_text(&s);
        CHECK(text != NULL);
        Settings back;
        settings_defaults(&back);
        SettingsLoadInfo info;
        settings_from_text(&back, text, &info);
        free(text);
        CHECK(back.show_ray == s.show_ray && back.pointer_dot == s.pointer_dot);
        Scene* next = scene_new();
        xrApplySettings(next->ctx, &back);
        CHECK(raySwitchOn(next->ctx->raySetting, next->ctx->rayFlipped) == (cell == 2));
        CHECK(next->ctx->dotOn == (cell >= 1));
        scene_free(next);

        // the bar button flips this session only: nothing in the file moves,
        // not even when the settings are rebuilt from the context
        setRayOn(c, cell != 2, "the bar button");
        CHECK(raySwitchOn(c->raySetting, c->rayFlipped) == (cell != 2));
        Settings keep = s;
        xrSettingsFromCtx(c, &s);
        CHECK(s.show_ray == keep.show_ray && s.pointer_dot == keep.pointer_dot);
        CHECK(!xrStoreSetting(c, &s, SETTING_POINTER_SLEEP, s.pointer_sleep));
        // and the cog's own choice afterwards replaces the flip
        memset(out, 0, sizeof(out));
        out[IN_SETTING] = -1;
        scene_press_option(c, COG_OPTION_RAY, 2, out);
        xrStoreSetting(c, &s, (int)out[IN_SETTING], (int)out[IN_SETTING_VALUE]);
        CHECK(s.show_ray == 1 && raySwitchOn(c->raySetting, c->rayFlipped) == 1);
        scene_free(sc);
    }
}

// The Controls tab's Show controllers row (Off / Frame / Gun): applied to the context,
// stored as the kind, round-tripped through config.ini into the next session, which loads that
// kind. Custom has no cell: it comes from the file, shows no cell marked, and survives the row's
// other cells being read
static void controllerModelRow(void) {
    CHECK(cogOptionCells(COG_OPTION_CONTROLLERS) == 3);
    for (int cell = 0; cell < 3; cell++) {
        Scene* sc = scene_new();
        XrCtx* c = sc->ctx;
        Settings s;
        settings_defaults(&s);
        float out[IN_SLOTS];
        memset(out, 0, sizeof(out));
        out[IN_SETTING] = -1;
        scene_press_option(c, COG_OPTION_CONTROLLERS, cell, out);
        CHECK((int)out[IN_SETTING] == SETTING_CONTROLLER_MODEL && (int)out[IN_SETTING_VALUE] == cell);
        CHECK(c->modelOn == (cell != 0) && c->modelKind == cell);
        CHECK(cogOptionValue(c, COG_OPTION_CONTROLLERS, 0) == cell);
        CHECK(xrStoreSetting(c, &s, (int)out[IN_SETTING], (int)out[IN_SETTING_VALUE]) == (cell != 0));
        CHECK(s.controller_model == cell);

        // through config.ini and into the next session
        char* text = settings_to_text(&s);
        CHECK(text != NULL);
        Settings back;
        settings_defaults(&back);
        settings_from_text(&back, text, NULL);
        free(text);
        CHECK(back.controller_model == cell);
        Scene* next = scene_new();
        xrApplySettings(next->ctx, &back);
        CHECK(next->ctx->modelOn == (cell != 0) && next->ctx->modelKind == cell);
        CHECK(controllerModelKind(next->ctx->modelOn, next->ctx->modelKind) == cell);
        scene_free(next);

        // and back out of the context
        Settings from;
        settings_defaults(&from);
        xrSettingsFromCtx(c, &from);
        CHECK(from.controller_model == cell);
        scene_free(sc);
    }

    // Custom is the file's: the context takes it, hands it back, and the row marks no cell
    Settings custom;
    settings_defaults(&custom);
    settings_from_text(&custom, "[vr]\ncontroller_model = custom\n", NULL);
    CHECK(custom.controller_model == CONTROLLER_MODEL_CUSTOM);
    Scene* sc = scene_new();
    xrApplySettings(sc->ctx, &custom);
    CHECK(sc->ctx->modelOn == 1 && sc->ctx->modelKind == CONTROLLER_MODEL_CUSTOM);
    CHECK(cogOptionValue(sc->ctx, COG_OPTION_CONTROLLERS, 0) == CONTROLLER_MODEL_CUSTOM);
    Settings back;
    settings_defaults(&back);
    xrSettingsFromCtx(sc->ctx, &back);
    CHECK(back.controller_model == CONTROLLER_MODEL_CUSTOM);
    // a stored value out of range is Off, not a crash
    CHECK(xrStoreSetting(sc->ctx, &back, SETTING_CONTROLLER_MODEL, 9) && back.controller_model == CONTROLLER_MODEL_OFF);
    scene_free(sc);

    // A file from before the row was a kind: true is the Frame's controllers, false none
    Settings old;
    settings_defaults(&old);
    settings_from_text(&old, "[vr]\ncontroller_model = true\n", NULL);
    Scene* oldScene = scene_new();
    xrApplySettings(oldScene->ctx, &old);
    CHECK(oldScene->ctx->modelOn == 1 && oldScene->ctx->modelKind == CONTROLLER_MODEL_FRAME);
    scene_free(oldScene);
    settings_from_text(&old, "[vr]\ncontroller_model = false\n", NULL);
    CHECK(old.controller_model == CONTROLLER_MODEL_OFF);
    // Lane fix-polish: the generic controller is gone; a file that picked it gets the Frame's
    settings_from_text(&old, "[vr]\ncontroller_model = generic\n", NULL);
    oldScene = scene_new();
    xrApplySettings(oldScene->ctx, &old);
    CHECK(old.controller_model == CONTROLLER_MODEL_FRAME && oldScene->ctx->modelOn == 1 &&
          oldScene->ctx->modelKind == CONTROLLER_MODEL_FRAME);
    CHECK(cogOptionValue(oldScene->ctx, COG_OPTION_CONTROLLERS, 0) == 1);   // the Frame cell
    scene_free(oldScene);
}

// Native (1.3.4): the passthrough sheets. In passthrough the controller models and the glow are held
// back until I Understand: Show controllers (greyed) and the glow's On ask on the hint sheet, Cancel
// changes nothing, I Understand stores its key (the glow's turns the glow on too) and lets them be drawn.
// The void is not affected.
static void passthroughSheets(void) {
    Scene* sc = scene_new();
    XrCtx* c = sc->ctx;
    Settings s;
    settings_defaults(&s);
    s.passthrough = 1;
    s.controller_model = CONTROLLER_MODEL_GUN;
    xrApplySettings(c, &s);
    CHECK(c->passthrough && c->modelOn && !c->ptControllersOk && !c->ptGlowOk);
    CHECK(xrPassthroughHidesModels(c) && xrPassthroughHidesGlow(c));
    CHECK(!controllerModelShown(c->modelOn, xrPassthroughHidesModels(c), PROFILE_CONTROLLER, 1, 0xFu));
    float out[IN_SLOTS];
    // Show controllers: any cell asks; Cancel keeps the model and stores nothing
    memset(out, 0, sizeof(out));
    out[IN_SETTING] = -1;
    c->cogOpen = 1;
    scene_press_option(c, COG_OPTION_CONTROLLERS, 0, out);
    CHECK((int)out[IN_SETTING] == -1 && c->hintOpen && c->hintKind == HINT_KIND_PT_CONTROLLERS && !c->cogOpen);
    CHECK(c->modelOn && c->modelKind == CONTROLLER_MODEL_GUN);
    ptSheetAnswer(c, 0, out);
    CHECK((int)out[IN_SETTING] == -1 && !c->hintOpen && c->cogOpen && xrPassthroughHidesModels(c));
    // I Understand: stored, and the Gun is drawn in passthrough
    scene_press_option(c, COG_OPTION_CONTROLLERS, 2, out);
    CHECK(c->hintOpen && c->hintKind == HINT_KIND_PT_CONTROLLERS);
    ptSheetAnswer(c, 1, out);
    CHECK((int)out[IN_SETTING] == SETTING_PT_CONTROLLERS_OK && (int)out[IN_SETTING_VALUE] == 1);
    CHECK(xrStoreSetting(c, &s, (int)out[IN_SETTING], (int)out[IN_SETTING_VALUE]) == 1);
    CHECK(s.passthrough_controllers_ok == 1 && s.controller_model == CONTROLLER_MODEL_GUN);
    CHECK(!xrPassthroughHidesModels(c) && c->modelKind == CONTROLLER_MODEL_GUN);
    CHECK(controllerModelShown(c->modelOn, xrPassthroughHidesModels(c), PROFILE_CONTROLLER, 1, 0xFu));
    // after it the row is an ordinary one: Off at once, no question
    memset(out, 0, sizeof(out));
    out[IN_SETTING] = -1;
    scene_press_option(c, COG_OPTION_CONTROLLERS, 0, out);
    CHECK(!c->hintOpen && (int)out[IN_SETTING] == SETTING_CONTROLLER_MODEL && !c->modelOn);

    // The glow: on in settings, but none in passthrough, and the row shows Off
    c->ambilightOn = 1;
    int on;
    float level;
    ambiEffective(c, &on, &level);
    CHECK(!on && cogOptionValue(c, COG_OPTION_AMBILIGHT, 0) == 0);
    // Off is no question
    memset(out, 0, sizeof(out));
    out[IN_SETTING] = -1;
    scene_press_option(c, COG_OPTION_AMBILIGHT, 0, out);
    CHECK(!c->hintOpen && (int)out[IN_SETTING] == SETTING_AMBILIGHT && !c->ambilightOn);
    // On asks; Cancel leaves it off
    memset(out, 0, sizeof(out));
    out[IN_SETTING] = -1;
    scene_press_option(c, COG_OPTION_AMBILIGHT, 1, out);
    CHECK(c->hintOpen && c->hintKind == HINT_KIND_PT_GLOW && (int)out[IN_SETTING] == -1 && !c->ambilightOn);
    ptSheetAnswer(c, 0, out);
    CHECK(!c->ambilightOn && !c->ptGlowOk && (int)out[IN_SETTING] == -1);
    // I Understand: on, kept, stored as both
    scene_press_option(c, COG_OPTION_AMBILIGHT, 1, out);
    ptSheetAnswer(c, 1, out);
    CHECK(c->ambilightOn && c->ptGlowOk && (int)out[IN_SETTING] == SETTING_PT_GLOW_OK);
    s.glow = 0;
    CHECK(xrStoreSetting(c, &s, (int)out[IN_SETTING], (int)out[IN_SETTING_VALUE]) == 1);
    CHECK(s.glow == 1 && s.passthrough_glow_ok == 1);
    ambiEffective(c, &on, &level);
    CHECK(on && cogOptionValue(c, COG_OPTION_AMBILIGHT, 0) == 1);
    Settings back;
    settings_defaults(&back);
    xrSettingsFromCtx(c, &back);
    CHECK(back.passthrough_glow_ok == 1 && back.passthrough_controllers_ok == 1 && back.glow == 1);
    scene_free(sc);

    // The void: no question, the glow and the models as before
    sc = scene_new();
    c = sc->ctx;
    settings_defaults(&s);
    s.controller_model = CONTROLLER_MODEL_FRAME;
    xrApplySettings(c, &s);
    CHECK(!c->passthrough && !xrPassthroughHidesModels(c) && !xrPassthroughHidesGlow(c));
    memset(out, 0, sizeof(out));
    out[IN_SETTING] = -1;
    scene_press_option(c, COG_OPTION_AMBILIGHT, 1, out);
    CHECK(!c->hintOpen && c->ambilightOn && (int)out[IN_SETTING] == SETTING_AMBILIGHT);
    scene_press_option(c, COG_OPTION_CONTROLLERS, 2, out);
    CHECK(!c->hintOpen && c->modelKind == CONTROLLER_MODEL_GUN);
    // and a file with the answers in it reads them
    s.passthrough = 1;
    s.passthrough_controllers_ok = 1;
    s.passthrough_glow_ok = 1;
    xrApplySettings(c, &s);
    CHECK(c->passthrough && !xrPassthroughHidesModels(c) && !xrPassthroughHidesGlow(c));
    scene_free(sc);
}

// Native (1.3.4): the Picture tab's Foveated rendering row: a press stores the choice (on or off,
// never auto), the ring follows it, auto shows Off, and the next session's start reads it
static void foveatedRow(void) {
    Scene* sc = scene_new();
    XrCtx* c = sc->ctx;
    Settings s;
    settings_defaults(&s);
    xrApplySettings(c, &s);
    CHECK(cogOptionCells(COG_OPTION_FOVEATED) == 2);
    CHECK(cogOptionValue(c, COG_OPTION_FOVEATED, 0) == 0);   // auto: Off
    float out[IN_SLOTS];
    for (int cell = 0; cell < 2; cell++) {
        memset(out, 0, sizeof(out));
        out[IN_SETTING] = -1;
        scene_press_option(c, COG_OPTION_FOVEATED, cell, out);
        CHECK((int)out[IN_SETTING] == SETTING_FOVEATED);
        int want = cell ? FOVEATED_ON : FOVEATED_OFF;
        CHECK((int)out[IN_SETTING_VALUE] == cell);
        CHECK(xrStoreSetting(c, &s, (int)out[IN_SETTING], (int)out[IN_SETTING_VALUE]) == 1);
        CHECK(s.foveated == want && cogOptionValue(c, COG_OPTION_FOVEATED, 0) == cell);
        Settings back;
        settings_defaults(&back);
        xrSettingsFromCtx(c, &back);
        CHECK(back.foveated == want);
    }
    // Either cell pressed while it shows auto is a choice: stored as on or off
    Settings fresh;
    settings_defaults(&fresh);
    CHECK(xrStoreSetting(c, &fresh, SETTING_FOVEATED, 1) == 1 && fresh.foveated == FOVEATED_ON);
    CHECK(xrStoreSetting(c, &fresh, SETTING_FOVEATED, 0) == 1 && fresh.foveated == FOVEATED_OFF);
    scene_free(sc);
}

// The Picture tab's Notices row (All / Important / Off): applied to the
// board, stored, round-tripped, and what it silences (the Hz toast, #42)
static void noticesRow(void) {
    for (int cell = 0; cell < 3; cell++) {
        Scene* sc = scene_new();
        XrCtx* c = sc->ctx;
        Settings s;
        settings_defaults(&s);
        float out[IN_SLOTS];
        memset(out, 0, sizeof(out));
        out[IN_SETTING] = -1;
        scene_press_option(c, COG_OPTION_NOTICES, cell, out);
        CHECK((int)out[IN_SETTING] == SETTING_NOTICES && (int)out[IN_SETTING_VALUE] == cell);
        CHECK(xrStoreSetting(c, &s, (int)out[IN_SETTING], (int)out[IN_SETTING_VALUE]) == (cell != 0));
        CHECK(s.notices == cell && c->notices.level == cell);
        CHECK(cogOptionValue(c, COG_OPTION_NOTICES, 0) == cell);
        CHECK(cogOptionCells(COG_OPTION_NOTICES) == 3);
        Settings back;
        settings_defaults(&back);
        xrSettingsFromCtx(c, &back);
        CHECK(back.notices == cell);
        Scene* next = scene_new();
        xrApplySettings(next->ctx, &s);
        CHECK(next->ctx->notices.level == cell);
        scene_free(next);

        // the display rate moving, once the session's first rate has landed
        c->rateSettled = 1;
        c->notices.count = 0;
        displayRateChanged(c, 90.0f, 72.0f);
        CHECK(c->notices.count == (cell == NOTICES_ALL ? 1 : 0));
        // a message (an error or a warning) is said at All and Important
        c->notices.count = 0;
        xrPanelsNotice(c, "Something went wrong");
        CHECK(c->notices.count == (cell == NOTICES_OFF ? 0 : 1));
        // so is a mode change
        c->notices.count = 0;
        noticePush(&c->notices, TOAST_HEAD_AIM_ON, 0);
        CHECK(c->notices.count == (cell == NOTICES_OFF ? 0 : 1));
        scene_free(sc);
    }
}

// The cog opens on the tab it was last left on: the tab bar press hands
// SETTING_COG_TAB back, it is stored, survives config.ini text, and the next
// session starts with the context on it. A tab that is not one is ignored.
static void cogTabKept(void) {
    for (int tab = 0; tab < COG_TAB_COUNT; tab++) {
        Scene* sc = scene_new();
        XrCtx* c = sc->ctx;
        Settings s;
        settings_defaults(&s);
        CHECK(c->cogTab == COG_TAB_SCREEN);
        // Native (1.3.4): the file keeps the six names it always had: Room's nearest is Screen,
        // Controls' is Display
        int file = cogTabToFile(tab);
        CHECK(file >= 0 && file < COG_FILE_TABS);
        CHECK(xrStoreSetting(c, &s, SETTING_COG_TAB, tab) == (file != 0));
        CHECK(s.cog_tab == file);
        char* text = settings_to_text(&s);
        CHECK(text != NULL);
        Settings back;
        settings_defaults(&back);
        SettingsLoadInfo info;
        settings_from_text(&back, text, &info);
        free(text);
        CHECK(back.cog_tab == file);
        Scene* next = scene_new();
        xrApplySettings(next->ctx, &back);
        CHECK(next->ctx->cogTab == cogTabFromFile(file));
        // and what the context holds is what a rebuild of the file says
        Settings live;
        settings_defaults(&live);
        xrSettingsFromCtx(next->ctx, &live);
        CHECK(live.cog_tab == cogTabToFile(cogTabFromFile(file)));
        // the running session keeps the tab itself, and a new choice in the file moves it
        c->cogTab = tab;
        xrApplySettings(c, &back);
        CHECK(c->cogTab == tab);
        back.cog_tab = tab == COG_TAB_3D ? 5 : 2;
        xrApplySettings(c, &back);
        CHECK(c->cogTab == (tab == COG_TAB_3D ? COG_TAB_ABOUT : COG_TAB_3D));
        scene_free(next);
        scene_free(sc);
    }
    // a stored tab out of range cannot put the context out of range
    Scene* sc = scene_new();
    Settings s;
    settings_defaults(&s);
    s.cog_tab = COG_TAB_COUNT + 3;
    xrApplySettings(sc->ctx, &s);
    CHECK(sc->ctx->cogTab == COG_TAB_SCREEN);
    scene_free(sc);
}

// How many of the frame's quads show a slot of the atlas
static int quadsOfSlot(const Scene* sc, int slot) {
    UiRectPx r = xrAtlasRect(slot);
    int n = 0;
    for (uint32_t i = 0; i < sc->out.count; i++) {
        const XrCompositionLayerQuad* q = (const XrCompositionLayerQuad*)sc->out.order[i];
        if (q->type == XR_TYPE_COMPOSITION_LAYER_QUAD && q->subImage.imageRect.offset.x == r.x
                && q->subImage.imageRect.offset.y == r.y && q->subImage.imageRect.extent.width == r.w
                && q->subImage.imageRect.extent.height == r.h) {
            n++;
        }
    }
    return n;
}

// With the Ray row on Off, a faint beam is drawn only while the controller is
// in the bar's hover band, so the bar can be found, or while a panel is up, so
// it can be aimed at; the Dot cell, the Ray cell and the rest of the view are
// not changed by it. Lane fix-polish: a panel up shows the row's choice live
// (Ray the beam, Dot no beam, Off the faint one) instead of the full beam
static void restingBeam(void) {
    Vec3 hand = { 0.2f, -0.3f, -0.3f }, at = { 0, -1.0f, -3.0f };
    for (int cell = 0; cell < 3; cell++) {
        Scene* sc = scene_new();
        XrCtx* c = sc->ctx;
        float out[IN_SLOTS];
        memset(out, 0, sizeof(out));
        out[IN_SETTING] = -1;
        scene_press_option(c, COG_OPTION_RAY, cell, out);
        // Off 0, Dot 1, Ray 2: only Off has neither the beam nor the dot
        int full = cell == 2 ? 1 : 0, faintInBand = cell == 0 ? 1 : 0;

        c->hoverKind = HOVER_SCREEN;
        scene_ray_to(sc, hand, at);
        scene_frame(sc, NULL);
        CHECK(quadsOfSlot(sc, ATLAS_BEAM) == full);
        CHECK(quadsOfSlot(sc, ATLAS_BEAM_FAINT) == 0);

        c->hoverKind = HOVER_BAR;
        scene_frame(sc, NULL);
        CHECK(quadsOfSlot(sc, ATLAS_BEAM) == full);
        CHECK(quadsOfSlot(sc, ATLAS_BEAM_FAINT) == faintInBand);

        // on a bar button too, and gone again once the ray leaves the band
        c->hoverKind = HOVER_COGBUTTON;
        scene_frame(sc, NULL);
        CHECK(quadsOfSlot(sc, ATLAS_BEAM_FAINT) == faintInBand);
        c->hoverKind = HOVER_NONE;
        scene_frame(sc, NULL);
        CHECK(quadsOfSlot(sc, ATLAS_BEAM_FAINT) == 0);

        // a panel up shows the cell as it is: the full beam on Ray, none on Dot (the dot is the
        // cue), the faint one on Off, in the band or out of it
        c->hoverKind = HOVER_BAR;
        c->cogOpen = 1;
        c->cogPose = cogPanelPose(c, &c->cogW, &c->cogH);
        scene_frame(sc, NULL);
        CHECK(quadsOfSlot(sc, ATLAS_BEAM) == full);
        CHECK(quadsOfSlot(sc, ATLAS_BEAM_FAINT) == faintInBand);
        c->hoverKind = HOVER_COGPANEL;
        scene_frame(sc, NULL);
        CHECK(quadsOfSlot(sc, ATLAS_BEAM) == full);
        CHECK(quadsOfSlot(sc, ATLAS_BEAM_FAINT) == faintInBand);
        // and a pick of another cell with the cog still open shows at once
        int next = (cell + 1) % 3;
        scene_press_option(c, COG_OPTION_RAY, next, out);
        scene_frame(sc, NULL);
        CHECK(quadsOfSlot(sc, ATLAS_BEAM) == (next == 2 ? 1 : 0));
        CHECK(quadsOfSlot(sc, ATLAS_BEAM_FAINT) == (next == 0 ? 1 : 0));
        scene_free(sc);
    }
    // the bar's own Ray button, which is the session's only: on the Ray cell,
    // switched off from the bar, the stub shows in the band
    Scene* sc = scene_new();
    XrCtx* c = sc->ctx;
    c->dotOn = 0;
    setRayOn(c, 0, "the bar button");
    c->hoverKind = HOVER_BAR;
    scene_ray_to(sc, hand, at);
    scene_frame(sc, NULL);
    CHECK(quadsOfSlot(sc, ATLAS_BEAM_FAINT) == 1 && quadsOfSlot(sc, ATLAS_BEAM) == 0);
    scene_free(sc);
}

// Native (lane wave2-bind-wire): the Controls tab's Click steadiness row (Off /
// Normal / Steady): applied to the press fix and the click hold, stored,
// round-tripped, and Normal is what the session had before the row
static void clickSteadyRow(void) {
    Scene* first = scene_new();
    PressFixCfg was = first->ctx->pressCfg;
    float wasHold = first->ctx->clickHoldPx;
    scene_free(first);
    for (int cell = 0; cell < 3; cell++) {
        Scene* sc = scene_new();
        XrCtx* c = sc->ctx;
        Settings s;
        settings_defaults(&s);
        float out[IN_SLOTS];
        memset(out, 0, sizeof(out));
        out[IN_SETTING] = -1;
        scene_press_option(c, COG_OPTION_CLICK_STEADY, cell, out);
        CHECK((int)out[IN_SETTING] == SETTING_CLICK_STEADY && (int)out[IN_SETTING_VALUE] == cell);
        CHECK(xrStoreSetting(c, &s, (int)out[IN_SETTING], (int)out[IN_SETTING_VALUE]) == (cell != CLICK_STEADY_NORMAL));
        CHECK(s.click_steadiness == cell && c->clickSteady == cell);
        CHECK(cogOptionValue(c, COG_OPTION_CLICK_STEADY, 0) == cell);
        CHECK(cogOptionCells(COG_OPTION_CLICK_STEADY) == 3);
        if (cell == CLICK_STEADY_OFF) {
            CHECK(c->pressCfg.stabilise == 0 && c->pressCfg.doubleNs == 0 && c->clickHoldPx == 0.0f);
        }
        else if (cell == CLICK_STEADY_NORMAL) {
            CHECK(c->pressCfg.stabilise == was.stabilise && c->pressCfg.doubleNs == was.doubleNs
                  && c->pressCfg.doublePx == was.doublePx && c->clickHoldPx == wasHold);
        }
        else {
            CHECK(c->pressCfg.stabilise == 1 && c->clickHoldPx == CLICK_STEADY_HOLD_PX);
        }
        Settings back;
        settings_defaults(&back);
        xrSettingsFromCtx(c, &back);
        CHECK(back.click_steadiness == cell);
        Scene* next = scene_new();
        xrApplySettings(next->ctx, &s);
        CHECK(next->ctx->clickSteady == cell && next->ctx->clickHoldPx == c->clickHoldPx);
        scene_free(next);
        scene_free(sc);
    }
}

// Which cell of a row of cells the art rings (the white edge of the one in
// force, at the left end of the cell's hit, halfway down) on the cog panel
// drawn from a view: -1 for none, -2 for more than one
static int drawnRing(const UiView* v, int row) {
    static uint8_t px[COG_TEX_W * COG_TEX_H * 4];
    static UiHits hits;
    UiCanvas cv;
    ui_cv_init(&cv, px, COG_TEX_W, COG_TEX_H);
    ui_cv_clear(&cv);
    ui_hits_clear(&hits);
    ui_draw_panel(UI_PANEL_COG, &cv, v, &hits);
    int found = -1;
    for (int i = 0; i < hits.count; i++) {
        const UiHit* h = &hits.hit[i];
        if (h->kind != UI_HIT_CELL || h->a != row) continue;
        int y = (int)((h->v0 + h->v1) * 0.5f * COG_TEX_H);
        int x0 = (int)(h->u0 * COG_TEX_W), x1 = x0 + (int)((h->u1 - h->u0) * COG_TEX_W * 0.1f);
        int peak = 0;
        for (int x = x0; x <= x1 && x < COG_TEX_W; x++) {
            int r = px[((size_t)y * COG_TEX_W + (size_t)x) * 4];
            if (r > peak) peak = r;
        }
        if (peak >= 0xE0) found = found == -1 ? h->b : -2;
    }
    return found;
}

// Every row of an option: the cell lane U rings is the one xr_ui.c says is in force,
// and the same rows are dead in both
static void optionCells(void) {
    Scene* s = scene_new();
    XrCtx* c = s->ctx;
    c->cogOpen = 1;
    c->cogTab = COG_TAB_ROOM;
    c->cogPose = cogPanelPose(c, &c->cogW, &c->cogH);
    for (int round = 0; round < 4; round++) {
        if (round == 3) {
            // Native (owner's request, 1.3.3): in a room, whose glow is its
            // own and off while the app wide one is on, the Glow row rings
            // the room's
            c->ambilightOn = 1;
            xrSetEnvironment(c, ENV_CELL_HOME_THEATER);
            c->roomGlow[ROOM_STYLE_THEATER] = 0;
        }
        if (round == 1) {
            c->sharpenMode = 0;
            c->overlayVisible = 1;
            c->pointerSleepOn = 0;
            setRayOn(c, 0, "test");
        }
        if (round == 2) {
            c->dotOn = 0;
            // the session hands the setting to the input pass every frame
            s->settings.head_locked = 1;
            c->headLockedPref = 1;
        }
        scene_frame(s, NULL);
        const UiView* v = xrPanelsLastView(c);
        for (int option = 0; option < COG_OPTION_COUNT; option++) {
            int tab = -1, row = -1;
            CHECK(cogOptionRow(option, &tab, &row) && cogRowOption(tab, row) == option);
            int live = cogRowLive(c, tab, row);
            CHECK(ui_cog_row_live(v, tab, row) == live);
            CHECK(live || option == COG_OPTION_HEAD_AIM || (round == 3 ? option == COG_OPTION_HEAD_LOCK : option == COG_OPTION_ROOM_LIGHT));
            if (!live) continue;
            int drawn = ui_cog_option_cell(v, option);
            int logic = cogOptionValue(c, option, s->settings.head_locked);
            CHECK(drawn == logic);
            if (drawn != logic) fprintf(stderr, "  round %d option %d: drawn %d, logic %d\n", round, option, drawn, logic);
        }
        CHECK(drawnRing(v, COG_ROOM_ROW_GLOW) == cogOptionValue(c, COG_OPTION_AMBILIGHT, 0));
        if (round == 3) CHECK(drawnRing(v, COG_ROOM_ROW_GLOW) == 0 && c->ambilightOn == 1);
    }
    xrSetEnvironment(c, ENV_CELL_VOID);
    scene_free(s);
}

// Native (owner's request, 1.3.3): in each room the Room tab's Glow row is the
// room's own glow: a press sets it, stored under the room, and the row rings it
// after; the app wide glow is left alone. Out of the room the row is the app
// wide switch again.
static void glowInRoom(void) {
    Scene* s = scene_new();
    XrCtx* c = s->ctx;
    c->cogOpen = 1;
    c->cogTab = COG_TAB_ROOM;
    c->cogPose = cogPanelPose(c, &c->cogW, &c->cogH);
    c->ambilightOn = 1;
    s->settings.glow = 1;
    float out[IN_SLOTS];
    static const int CELLS[] = { ENV_CELL_HOME_THEATER, ENV_CELL_GRAND_CINEMA, ENV_CELL_SYNTHWAVE };
    for (int k = 0; k < 3; k++) {
        xrSetEnvironment(c, CELLS[k]);
        int style = roomEffective(c);
        CHECK(style >= ROOM_STYLE_FIRST && style <= ROOM_STYLE_LAST);
        if (style < ROOM_STYLE_FIRST || style > ROOM_STYLE_LAST) continue;
        int r = style - ROOM_STYLE_FIRST;
        c->roomGlow[style] = 0;
        s->settings.room[r].glow = 0;
        for (int p = -1; p < 4; p++) {
            int want = p < 0 ? 0 : !(p & 1);   // as it was, on, off, on, off
            if (p >= 0) {
                memset(out, 0, sizeof(out));
                out[IN_SETTING] = -1.0f;
                cogApplyCell(c, COG_TAB_ROOM, COG_ROOM_ROW_GLOW, want, out);
                CHECK((int)out[IN_SETTING] == SETTING_ROOM_GLOW && (int)out[IN_SETTING_VALUE] == want);
                xrStoreSetting(c, &s->settings, (int)out[IN_SETTING], (int)out[IN_SETTING_VALUE]);
                CHECK(s->settings.room[r].glow == want);
            }
            CHECK(c->roomGlow[style] == want && roomGlowOn(c, style) == want);
            CHECK(c->ambilightOn == 1 && s->settings.glow == 1);
            scene_frame(s, NULL);
            const UiView* v = xrPanelsLastView(c);
            CHECK(cogRowLive(c, COG_TAB_ROOM, COG_ROOM_ROW_GLOW));
            CHECK(ui_cog_row_live(v, COG_TAB_ROOM, COG_ROOM_ROW_GLOW));
            CHECK(cogOptionValue(c, COG_OPTION_AMBILIGHT, 0) == want);
            CHECK(cogCellInForce(c, COG_TAB_ROOM, COG_ROOM_ROW_GLOW) == want);
            CHECK(drawnRing(v, COG_ROOM_ROW_GLOW) == want);
        }
        // a room left on, for the way out
        c->roomGlow[style] = 1;
        s->settings.room[r].glow = 1;
    }
    // Out of the rooms: the app wide switch, drawn and pressed
    xrSetEnvironment(c, ENV_CELL_VOID);
    c->ambilightOn = 0;
    scene_frame(s, NULL);
    CHECK(drawnRing(xrPanelsLastView(c), COG_ROOM_ROW_GLOW) == 0);
    memset(out, 0, sizeof(out));
    out[IN_SETTING] = -1.0f;
    cogApplyCell(c, COG_TAB_ROOM, COG_ROOM_ROW_GLOW, 1, out);
    CHECK((int)out[IN_SETTING] == SETTING_AMBILIGHT && c->ambilightOn == 1);
    scene_frame(s, NULL);
    CHECK(drawnRing(xrPanelsLastView(c), COG_ROOM_ROW_GLOW) == 1);
    scene_free(s);
}

// Lane av-latency: the pointer's One Euro filter at the native settings
// (min cutoff 1 Hz, beta 0.015 per pixel a second) lags a hand moving at
// 400 px/s by ~12 ms at 90 Hz (0.01 per px gave ~15), and the environment
// overrides reach the context
static double rampLagMs(const XrCtx* ctx, float pxPerSec) {
    EuroState f = { 0 };
    float dt = 1.0f / 90.0f, w = (float)ctx->videoWidth;
    for (int i = 0; i < 90; i++) euroFilter(&f, 0.3f, dt, ctx->pointerMinCutoff, ctx->pointerBeta);
    float x = 0.3f, y = 0.3f;
    for (int i = 0; i < 45; i++) {
        x += pxPerSec * dt / w;
        y = euroFilter(&f, x, dt, ctx->pointerMinCutoff, ctx->pointerBeta);
    }
    return 1000.0 * (x - y) * w / pxPerSec;
}

static void pointerSmoothing(void) {
    unsetenv("MLXR_POINTER_BETA");
    unsetenv("MLXR_POINTER_MIN_CUTOFF");
    Scene* s = scene_new();
    CHECK(s->ctx->pointerMinCutoff == 1.0f);
    CHECK_NEAR(s->ctx->pointerBeta, 0.015 * s->ctx->videoWidth, 1e-3);
    double lag400 = rampLagMs(s->ctx, 400.0f), lag1500 = rampLagMs(s->ctx, 1500.0f);
    CHECK(lag400 > 8.0 && lag400 < 14.0);
    CHECK(lag1500 > 2.0 && lag1500 < 6.0);
    scene_free(s);
    setenv("MLXR_POINTER_BETA", "0.01", 1);
    setenv("MLXR_POINTER_MIN_CUTOFF", "1.5", 1);
    s = scene_new();
    CHECK_NEAR(s->ctx->pointerBeta, 0.01 * s->ctx->videoWidth, 1e-3);
    CHECK(s->ctx->pointerMinCutoff == 1.5f);
    CHECK(rampLagMs(s->ctx, 400.0f) > lag400);
    scene_free(s);
    unsetenv("MLXR_POINTER_BETA");
    unsetenv("MLXR_POINTER_MIN_CUTOFF");
}

// Native (lane wave4b-abr): the bitrate a stream starts with on the path in use (xrStreamBitrate)
static void pathBitrates(void) {
    Settings s;
    settings_defaults(&s);
    CHECK(xrStreamBitrate(&s, MLXR_ABR_WIFI, 98000, 2560, 1440, 90, NULL, 0) == 98000);   // no override anywhere: the launcher's
    s.bitrate_wifi = 60000;
    s.bitrate_adapter = 150000;
    CHECK(xrStreamBitrate(&s, MLXR_ABR_WIFI, 98000, 2560, 1440, 90, NULL, 0) == 60000);
    CHECK(xrStreamBitrate(&s, MLXR_ABR_ADAPTER, 98000, 2560, 1440, 90, NULL, 0) == 150000);
    CHECK(xrStreamBitrate(&s, MLXR_ABR_WIRED, 98000, 2560, 1440, 90, NULL, 0) == 98000);     // that path has none
    CHECK(xrStreamBitrate(&s, MLXR_ABR_UNKNOWN, 98000, 2560, 1440, 90, NULL, 0) == 98000);   // no path chosen: none applies
}

// Native (lane flat-input): 300 Mbps from each place it can be set survives config.ini's parser and the start decision,
// to the figure the stream's configuration carries (app/main.c: cfg.bitrate = xrStreamBitrate(...)); the owner saw
// nothing above 150 Mbps
static void bitrate300(void) {
    Settings s;
    char why[128];
    // --bitrate 300000 (the flat launcher's Video bitrate slider or its Custom bitrate row)
    settings_defaults(&s);
    CHECK(xrStreamBitrate(&s, MLXR_ABR_WIFI, 300000, 2560, 1440, 90, why, sizeof(why)) == 300000);
    CHECK(strcmp(why, "the flat launcher's Video bitrate (--bitrate)") == 0);
    // config.ini [stream] bitrate_kbps = 300000, no --bitrate
    SettingsLoadInfo info;
    settings_defaults(&s);
    settings_from_text(&s, "[stream]\nbitrate_kbps = 300000\n", &info);
    CHECK(info.clamped == 0);
    CHECK(xrStreamBitrate(&s, MLXR_ABR_WIRED, 0, 2560, 1440, 90, why, sizeof(why)) == 300000);
    CHECK(strcmp(why, "config.ini [stream] bitrate_kbps") == 0);
    // a connection's row: [stream] bitrate_wifi = 300000, over Wi-Fi, whatever --bitrate says
    settings_defaults(&s);
    settings_from_text(&s, "[stream]\nbitrate_wifi = 300000\nbitrate_usb = 450000\n", &info);
    CHECK(info.clamped == 0 && s.bitrate_wifi == 300000);
    CHECK(xrStreamBitrate(&s, MLXR_ABR_WIFI, 98000, 2560, 1440, 90, why, sizeof(why)) == 300000);
    CHECK(strcmp(why, "config.ini [stream] bitrate_wifi (Bitrate over Wi-Fi)") == 0);
    CHECK(xrStreamBitrate(&s, MLXR_ABR_USB, 98000, 2560, 1440, 90, NULL, 0) == 450000);
    CHECK(xrStreamBitrate(&s, MLXR_ABR_ADAPTER, 98000, 2560, 1440, 90, NULL, 0) == 98000);
    // nothing set: the default curve (which alone stops at 150 Mbps)
    settings_defaults(&s);
    CHECK(xrStreamBitrate(&s, MLXR_ABR_WIFI, 0, 2560, 1440, 90, why, sizeof(why)) == settings_default_bitrate_kbps(2560, 1440, 90));
    CHECK(strcmp(why, "the default for this resolution and frame rate") == 0);
    // above the 500 Mbps cap: held there
    CHECK(xrStreamBitrate(&s, MLXR_ABR_WIFI, 900000, 2560, 1440, 90, NULL, 0) == 500000);
}

int main(void) {
    pathBitrates();
    bitrate300();
    pointerSmoothing();
    roundTripDefaults();
    roundTripChanged();
    poseRoundTrips();
    thumbsAgree();
    surroundingsRows();
    storeSettings();
    rayRowSaved();
    noticesRow();
    foveatedRow();
    passthroughSheets();
    clickSteadyRow();
    controllerModelRow();
    cogTabKept();
    restingBeam();
    optionCells();
    glowInRoom();
    depthRows();
    return checksDone("m4 prefs");
}
