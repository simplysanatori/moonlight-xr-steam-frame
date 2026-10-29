// Moonlight XR Native, tests/m4: config.ini <-> XrCtx (xr_prefs.c), the way
// upstream's nativeInit arguments and JNI setters fed the renderer and
// XrRenderer.applySetting / saveScreenPose stored what it handed back.
// GPLv3.
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "scene.h"

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
    in.stats = 1;
    in.glow = 1;
    in.glow_level = 35;
    in.screen_light = 0;
    in.pointer_sleep = 0;
    in.controller_model = 1;
    in.click_sound = 0;
    in.pointer_dot = 0;
    in.head_aim_sensitivity = HEAD_AIM_SENSITIVITY_MAX;
    in.head_aim_deadzone = HEAD_AIM_DEADZONE_MIN;
    in.picture_brightness = 12;
    in.picture_contrast = 120;
    in.picture_gamma = 90;
    in.picture_saturation = 140;
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
    xrApplySettings(ctx, &in);
    // what the context took
    CHECK(ctx->sharpenMode == 1 && ctx->supersampleMode == 2 && ctx->overlayVisible == 1);
    CHECK(ctx->dotOn == 0 && ctx->pointerSleepOn == 0 && ctx->modelOn == 1 && ctx->clickSoundOn == 0);
    CHECK(ctx->gradeOn == 1);
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
    CHECK(back.glow_level == 35 && back.screen_light == 0 && back.pointer_sleep == 0);
    CHECK(back.controller_model == 1 && back.click_sound == 0 && back.pointer_dot == 0);
    CHECK(back.head_aim_sensitivity == in.head_aim_sensitivity && back.head_aim_deadzone == in.head_aim_deadzone);
    CHECK(back.picture_brightness == 12 && back.picture_contrast == 120 && back.picture_gamma == 90
          && back.picture_saturation == 140);
    CHECK(back.screen_distance_cm == 250 && back.screen_height_cm == 30 && back.screen_azimuth_deg == 20);
    CHECK(back.screen_tilt_deg == -10 && back.screen_rotate_deg == 15 && back.screen_curve == 40
          && back.screen_width_cm == 400);
    CHECK(back.room[1].brightness == 80);
    free(ctx);
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
    // The picture rows and head aim's: through pictureSet and config.ini
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
        cogApplyCell(c, COG_TAB_DISPLAY, COG_OPTION_RAY, cell, out);
        CHECK((int)out[IN_SETTING] == SETTING_POINTER_DOT);
        CHECK(cogOptionValue(c, COG_OPTION_RAY, 0) == cell);
        xrStoreSetting(c, &s, (int)out[IN_SETTING], (int)out[IN_SETTING_VALUE]);
        CHECK(s.pointer_dot == (cell >= 1));
        CHECK(raySwitchOn(c->raySetting, c->rayFlipped) == (cell == 2));
    }
    scene_free(sc);
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
        cogApplyCell(c, COG_TAB_DISPLAY, COG_OPTION_RAY, cell, out);
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
        cogApplyCell(c, COG_TAB_DISPLAY, COG_OPTION_RAY, 2, out);
        xrStoreSetting(c, &s, (int)out[IN_SETTING], (int)out[IN_SETTING_VALUE]);
        CHECK(s.show_ray == 1 && raySwitchOn(c->raySetting, c->rayFlipped) == 1);
        scene_free(sc);
    }
}

// The Display tab's Notices row (All / Important / Off): applied to the
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
        cogApplyCell(c, COG_TAB_DISPLAY, COG_OPTION_NOTICES, cell, out);
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

// The Display tab: the cell lane U rings is the one xr_ui.c says is in force,
// and the rows that come later are dead in both
static void displayCells(void) {
    Scene* s = scene_new();
    XrCtx* c = s->ctx;
    c->cogOpen = 1;
    c->cogTab = COG_TAB_DISPLAY;
    c->cogPose = cogPanelPose(c, &c->cogW, &c->cogH);
    for (int round = 0; round < 3; round++) {
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
        for (int row = 0; row < COG_OPTION_COUNT; row++) {
            int live = cogRowLive(c, COG_TAB_DISPLAY, row);
            CHECK(ui_cog_row_live(v, COG_TAB_DISPLAY, row) == live);
            CHECK(live || xrRowLater(c, COG_TAB_DISPLAY, row) != 0 || row == COG_OPTION_HEAD_AIM);
            if (!live) continue;
            int drawn = settings_option_cell(v->settings, &v->session, row);
            int logic = cogOptionValue(c, row, s->settings.head_locked);
            CHECK(drawn == logic);
            if (drawn != logic) fprintf(stderr, "  round %d row %d: drawn %d, logic %d\n", round, row, drawn, logic);
        }
    }
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

int main(void) {
    pointerSmoothing();
    roundTripDefaults();
    roundTripChanged();
    poseRoundTrips();
    thumbsAgree();
    storeSettings();
    rayRowSaved();
    noticesRow();
    displayCells();
    return checksDone("m4 prefs");
}
