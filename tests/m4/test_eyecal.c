// Moonlight XR Native, tests/m4: the eye calibration (lane eye-calibration), driven through the input
// pass's seams. A scripted gaze that misses where it looks by an offset and a scale (and a head that
// turns a little all through) follows the twelve dots from the Accessibility tab's Start: the fit is
// kept, handed back to be stored in config.ini and read back from it, and the Eyes pointer then lands
// where the eyes look (and misses again after Reset). The row is live only with the eyes reaching the
// app; B, Menu and the panel closing cancel a run and keep the calibration in force; a gaze too
// unsteady fails it with the old one kept; no dwell clicks during a run; and nothing at all changes
// the gaze with no calibration stored. GPLv3.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "scene.h"

#define FAKE(n) ((uintptr_t)(n))
#define VIEW ((XrSpace)FAKE(0x503))
#define LOCAL ((XrSpace)FAKE(0x504))
#define GAZE ((XrSpace)FAKE(0x505))
#define FRAME_NS 11111111LL

// The head's yaw (degrees, turning left positive about +y), the point the eyes truly look at (local
// space, from the origin), whether they are tracked, and how the eye tracker gets them wrong: yaw and
// pitch in the head's frame scaled, then offset, then jittered by up to g_jitter degrees
static float g_headYaw;
static Vec3 g_look = { 0.0f, 0.0f, -1.0f };
static int g_eyesTracked;
static float g_scale = 1.0f, g_offYaw, g_offPitch, g_jitter;
static uint32_t g_seed = 99u;

static float noise(void) {
    g_seed = g_seed * 1664525u + 1013904223u;
    return (float)((g_seed >> 8) & 0xFFFFu) / 32767.5f - 1.0f;
}

static XrQuaternionf headQ(void) {
    Vec3 up = { 0.0f, 1.0f, 0.0f };
    return axisAngleQuat(up, g_headYaw * (float)M_PI / 180.0f);
}

// An orientation whose -z is d, upright
static XrQuaternionf lookAlong(Vec3 d) {
    Vec3 up = { 0.0f, 1.0f, 0.0f };
    Vec3 right = { 1.0f, 0.0f, 0.0f };
    float yaw = atan2f(-d.x, -d.z);
    float pitch = atan2f(d.y, sqrtf(d.x * d.x + d.z * d.z));
    return quatNorm(quatMul(axisAngleQuat(up, yaw), axisAngleQuat(right, pitch)));
}

static XrQuaternionf measuredGaze(void) {
    XrQuaternionf h = headQ();
    Vec3 inHead = quatRotate(quatConj(h), vecNorm(g_look));
    float v[3] = { inHead.x, inHead.y, inHead.z }, yaw, pitch, d[3];
    eyeCalAngles(v, &yaw, &pitch);
    yaw = yaw * g_scale + g_offYaw + noise() * g_jitter;
    pitch = pitch * g_scale + g_offPitch + noise() * g_jitter;
    eyeCalDir(yaw, pitch, d);
    Vec3 back = quatRotate(h, (Vec3){ d[0], d[1], d[2] });
    return lookAlong(back);
}

static XrResult fakeLocate(XrCtx* ctx, XrSpace space, XrSpace base, XrTime time, XrSpaceLocation* out) {
    (void)ctx; (void)base; (void)time;
    const XrSpaceLocationFlags all = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT
            | XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
    out->pose.position = (XrVector3f){ 0.0f, 0.0f, 0.0f };
    if (space == VIEW) {
        out->locationFlags = all;
        out->pose.orientation = headQ();
        return XR_SUCCESS;
    }
    if (space == GAZE) {
        out->locationFlags = g_eyesTracked ? all : 0;
        out->pose.orientation = measuredGaze();
        return XR_SUCCESS;
    }
    return XR_ERROR_HANDLE_INVALID;
}

static int fakePoseActive(XrCtx* ctx, XrAction action, int hand) {
    (void)hand;
    return action == ctx->gazeAction && g_eyesTracked;
}

static float fakeFloat(XrCtx* ctx, XrAction action, int hand) {
    (void)ctx; (void)action; (void)hand;
    return 0.0f;
}

static int fakeBool(XrCtx* ctx, XrAction action, int hand) {
    (void)ctx; (void)action; (void)hand;
    return 0;
}

static XrVector2f fakeVec2(XrCtx* ctx, XrAction action, int hand) {
    (void)ctx; (void)action; (void)hand;
    XrVector2f zero = { 0.0f, 0.0f };
    return zero;
}

static int64_t g_now = 1000000000LL;
static int g_settingsOut, g_lastSetting = -1, g_lastValue = -1;
static Settings g_file;   // config.ini as the session keeps it

static void setUp(Scene* s) {
    XrCtx* c = s->ctx;
    scene_reset(s);
    float place[10] = { 0.0f, 0.0f, -2.5f, 0.0f, 0.0f, 0.0f, 1.0f, 2.4f, 2.5f, -1.0f };
    xrSetScreenPose(c, place);
    c->viewSpace = VIEW;
    c->localSpace = LOCAL;
    c->aimSpaces[0] = c->aimSpaces[1] = XR_NULL_HANDLE;
    c->profileKind[0] = c->profileKind[1] = PROFILE_NONE;
    c->locateSpace = fakeLocate;
    c->poseActive = fakePoseActive;
    c->readFloat = fakeFloat;
    c->readBool = fakeBool;
    c->readVec2 = fakeVec2;
    c->pointerSleepOn = 0;
    c->pointerAwake = 1;
    c->padMode = 0;
    c->eyeGaze = c->gazeOffered = c->gazeBound = c->gazeSupportFlag = 1;
    c->gazeAsked = 0;
    c->gazeSeen = c->gazeReported = 0;
    c->gazeAction = (XrAction)FAKE(0x601);
    c->aimSpaces[SRC_GAZE] = GAZE;
    c->gazeEnabled = 0;
    xrSetPointerSource(c, POINTER_SOURCE_GAZE, 0);
    c->cogDragSlider = -1;
    c->cogOpen = 1;
    c->cogTab = COG_TAB_ACCESS;
    c->panelFades[FADE_COG].level = 1.0f;
    c->cogPose = cogPanelPose(c, &c->cogW, &c->cogH);
    memset(&c->padSeen, 0, sizeof(c->padSeen));
    g_headYaw = 0.0f;
    g_look = (Vec3){ 0.0f, 0.0f, -1.0f };
    g_eyesTracked = 1;
    g_jitter = 0.0f;
}

// Frames of the pointer and the open panel; a setting handed back is stored in g_file
static void frames(XrCtx* c, int n) {
    float out[IN_SLOTS];
    for (int i = 0; i < n; i++) {
        g_now += FRAME_NS;
        c->predictedDisplayTime = (XrTime)g_now;
        xrAssistFrame(c, g_now);
        xrPointerPanelStep(c, g_now, 0, out);
        if (out[IN_SETTING] >= 0.0f) {
            g_settingsOut++;
            g_lastSetting = (int)out[IN_SETTING];
            g_lastValue = (int)out[IN_SETTING_VALUE];
            xrStoreSetting(c, &g_file, g_lastSetting, g_lastValue);
        }
    }
}

static Vec3 panelAt(XrCtx* c, float u, float v) {
    Vec3 local = { (u - 0.5f) * c->cogW, (0.5f - v) * c->cogH, 0.0f };
    Vec3 off = quatRotate(c->cogPose.orientation, local);
    return (Vec3){ c->cogPose.position.x + off.x, c->cogPose.position.y + off.y, c->cogPose.position.z + off.z };
}

static void press(XrCtx* c, int cell) {
    float out[IN_SLOTS];
    out[IN_SETTING] = -1.0f;
    cogApplyCell(c, COG_TAB_ACCESS, COG_ACCESS_ROW_EYECAL, cell, out);
    if (out[IN_SETTING] >= 0.0f) {
        g_settingsOut++;
        g_lastSetting = (int)out[IN_SETTING];
        g_lastValue = (int)out[IN_SETTING_VALUE];
        xrStoreSetting(c, &g_file, g_lastSetting, g_lastValue);
    }
}

// The eyes follow the dots until the run ends (or maxFrames), the head turning slowly to and fro by
// 4 degrees all the while; returns the frames it took
static int followDots(XrCtx* c, int maxFrames) {
    int n = 0;
    for (; n < maxFrames && xrEyeCalUp(c); n++) {
        g_headYaw = 4.0f * sinf((float)n * 0.01f);
        Vec3 at;
        float size;
        Vec3 head = { 0.0f, 0.0f, 0.0f };
        if (xrEyeCalDotPoint(c, head, g_now, &at, &size)) {
            g_look = at;
        }
        frames(c, 1);
    }
    g_headYaw = 0.0f;
    return n;
}

// The v of the panel the Eyes pointer settles on while the eyes truly look at (u, v)
static float settledV(XrCtx* c, float u, float v) {
    g_look = panelAt(c, u, v);
    frames(c, 90);
    return c->cogPointV;
}

static void offeredOnlyWithEyes(Scene* s) {
    XrCtx* c = s->ctx;
    setUp(s);
    // eye gaze offered but none yet: the row is dead, and a press on Start does nothing
    CHECK(xrAccessEyes(c) == ACCESS_EYES_WAITING);
    CHECK(cogAccessCells(c, COG_ACCESS_ROW_EYECAL) == 2 && cogRowCells(COG_TAB_ACCESS, COG_ACCESS_ROW_EYECAL) == 2);
    CHECK(!cogRowLive(c, COG_TAB_ACCESS, COG_ACCESS_ROW_EYECAL));
    press(c, COG_ACCESS_EYECAL_START);
    CHECK(!xrEyeCalUp(c));
    // the eyes arrive: Start is live, Reset not until there is a calibration
    frames(c, 5);
    CHECK(xrAccessEyes(c) == ACCESS_EYES_READY);
    CHECK(cogRowLive(c, COG_TAB_ACCESS, COG_ACCESS_ROW_EYECAL));
    CHECK(cogAccessCellLive(c, COG_ACCESS_ROW_EYECAL, COG_ACCESS_EYECAL_START));
    CHECK(!cogAccessCellLive(c, COG_ACCESS_ROW_EYECAL, COG_ACCESS_EYECAL_RESET));
    // live whatever points
    xrSetPointerSource(c, POINTER_SOURCE_CONTROLLER, 0);
    CHECK(cogRowLive(c, COG_TAB_ACCESS, COG_ACCESS_ROW_EYECAL));
    xrSetPointerSource(c, POINTER_SOURCE_GAZE, 0);
}

static void calibrates(Scene* s) {
    XrCtx* c = s->ctx;
    setUp(s);
    settings_defaults(&g_file);
    g_scale = 1.08f;
    g_offYaw = 2.5f;
    g_offPitch = -1.5f;
    g_jitter = 0.6f;
    frames(c, 5);
    // uncorrected, the Eyes pointer lands well off where the eyes look
    float before = settledV(c, 0.5f, 0.55f);
    CHECK(fabsf(before - 0.55f) > 0.05f);
    // a dwell on, so the run can show it never clicks
    xrSetPointerSource(c, POINTER_SOURCE_GAZE, 600);
    long clicks = c->dwellClicks;
    g_settingsOut = 0;
    press(c, COG_ACCESS_EYECAL_START);
    CHECK(xrEyeCalUp(c) && xrEyeCalStep(c) == 1 && g_settingsOut == 0);
    // the panel shows the run's face, and the dot is in the frame's layers
    scene_frame(s, NULL);
    xrPanelsFlush(c);
    const UiView* v = xrPanelsLastView(c);
    CHECK(v != NULL && v->eyecal_step == 1);
    int took = followDots(c, 90 * 30);
    CHECK(!xrEyeCalUp(c));
    CHECK(took > 90 * 13 && took < 90 * 16);   // 12 dots of 1.2 s
    CHECK(c->dwellClicks == clicks);
    xrSetPointerSource(c, POINTER_SOURCE_GAZE, 0);
    CHECK(c->eyeCalResult == EYECAL_OK && c->eyeCal.model != EYECAL_MODEL_NONE);
    CHECK(c->eyeCal.used >= EYECAL_MIN_DOTS);
    CHECK(c->eyeCal.meanBefore > 2.0f && c->eyeCal.meanAfter < 0.5f);
    printf("  calibrated: %d dots, %d dropped, %.2f -> %.2f deg average, %.2f -> %.2f largest\n", c->eyeCal.used,
           c->eyeCal.dropped, c->eyeCal.meanBefore, c->eyeCal.meanAfter, c->eyeCal.maxBefore, c->eyeCal.maxAfter);
    // handed back once, and stored
    CHECK(g_settingsOut == 1 && g_lastSetting == SETTING_EYE_CAL && g_lastValue == 1);
    EyeCal stored;
    CHECK(eyeCalParse(g_file.eye_calibration, &stored) && stored.model == c->eyeCal.model);
    CHECK_NEAR(stored.meanAfter, c->eyeCal.meanAfter, 0.006);
    CHECK(cogAccessCellLive(c, COG_ACCESS_ROW_EYECAL, COG_ACCESS_EYECAL_RESET));
    // the panel says it
    scene_frame(s, NULL);
    xrPanelsFlush(c);
    v = xrPanelsLastView(c);
    CHECK(v != NULL && v->eyecal_step == 0 && v->access_cal == EYECAL_OK);
    CHECK(eyeCalParse(v->settings->eye_calibration, &stored));
    // and the Eyes pointer now lands where the eyes look, at the middle and toward a corner
    g_jitter = 0.3f;
    float after = settledV(c, 0.5f, 0.55f);
    CHECK(fabsf(after - 0.55f) < 0.02f);
    CHECK(fabsf(settledV(c, 0.3f, 0.3f) - 0.3f) < 0.025f);
    printf("  the Eyes pointer at v 0.55: %.3f uncorrected, %.3f corrected\n", before, after);
    // config.ini read back into a fresh context puts it in force
    char* text = settings_to_text(&g_file);
    Settings back;
    settings_defaults(&back);
    settings_from_text(&back, text ? text : "", NULL);
    free(text);
    CHECK(strcmp(back.eye_calibration, g_file.eye_calibration) == 0);
    XrCtx* fresh = calloc(1, sizeof(XrCtx));
    xrCtxDefaults(fresh, 2560, 1440, 90);
    xrApplySettings(fresh, &back);
    CHECK(fresh->eyeCal.model == stored.model);
    CHECK_NEAR(fresh->eyeCal.c[0][0], stored.c[0][0], 1e-6);
    free(fresh);
}

static void cancels(Scene* s) {
    XrCtx* c = s->ctx;
    EyeCal kept = c->eyeCal;
    CHECK(kept.model != EYECAL_MODEL_NONE);
    g_settingsOut = 0;
    // B, held from before the run: no cancel until pressed again
    c->padSeen[HAND_RIGHT].upper = 1;
    press(c, COG_ACCESS_EYECAL_START);
    followDots(c, 60);
    CHECK(xrEyeCalUp(c));
    c->padSeen[HAND_RIGHT].upper = 0;
    frames(c, 2);
    CHECK(xrEyeCalUp(c));
    c->padSeen[HAND_RIGHT].upper = 1;
    frames(c, 1);
    CHECK(!xrEyeCalUp(c) && c->cogOpen);
    c->padSeen[HAND_RIGHT].upper = 0;
    // Menu
    press(c, COG_ACCESS_EYECAL_START);
    followDots(c, 30);
    c->padSeen[HAND_RIGHT].start = 1;
    frames(c, 1);
    CHECK(!xrEyeCalUp(c));
    c->padSeen[HAND_RIGHT].start = 0;
    frames(c, 2);
    // the panel closed
    press(c, COG_ACCESS_EYECAL_START);
    followDots(c, 30);
    c->cogOpen = 0;
    frames(c, 1);
    CHECK(!xrEyeCalUp(c));
    c->cogOpen = 1;
    // nothing stored, the calibration in force as it was
    CHECK(g_settingsOut == 0 && c->eyeCal.model == kept.model && c->eyeCal.c[0][0] == kept.c[0][0]);
    CHECK(c->eyeCalResult == -1);
}

static void failsKeepsOld(Scene* s) {
    XrCtx* c = s->ctx;
    EyeCal kept = c->eyeCal;
    g_settingsOut = 0;
    g_jitter = 4.0f;   // nothing steady
    press(c, COG_ACCESS_EYECAL_START);
    int took = followDots(c, 90 * 40);
    CHECK(!xrEyeCalUp(c));
    CHECK(took > 90 * 25);   // every dot tried twice
    CHECK(c->eyeCalResult == EYECAL_FAIL_FEW);
    CHECK(g_settingsOut == 0 && c->eyeCal.c[0][0] == kept.c[0][0] && c->eyeCal.model == kept.model);
    g_jitter = 0.3f;
    // the gaze lost for good: the same
    press(c, COG_ACCESS_EYECAL_START);
    g_eyesTracked = 0;
    followDots(c, 90 * 40);
    g_eyesTracked = 1;
    CHECK(c->eyeCalResult == EYECAL_FAIL_FEW && c->eyeCal.c[0][0] == kept.c[0][0]);
}

static void resets(Scene* s) {
    XrCtx* c = s->ctx;
    g_settingsOut = 0;
    press(c, COG_ACCESS_EYECAL_RESET);
    CHECK(c->eyeCal.model == EYECAL_MODEL_NONE && c->eyeCalResult == -1);
    CHECK(g_settingsOut == 1 && g_lastSetting == SETTING_EYE_CAL && g_lastValue == 0);
    CHECK(g_file.eye_calibration[0] == 0);
    CHECK(!cogAccessCellLive(c, COG_ACCESS_ROW_EYECAL, COG_ACCESS_EYECAL_RESET));
    // the pointer misses again
    CHECK(fabsf(settledV(c, 0.5f, 0.55f) - 0.55f) > 0.05f);
    // with none stored the gaze goes through untouched, bit for bit
    XrQuaternionf h = headQ(), g = measuredGaze();
    XrQuaternionf same = xrEyeCalCorrect(c, h, g);
    CHECK(memcmp(&same, &g, sizeof(g)) == 0);
    g_scale = 1.0f;
    g_offYaw = g_offPitch = 0.0f;
    g_jitter = 0.0f;
}

int main(void) {
    Scene* s = scene_new();
    offeredOnlyWithEyes(s);
    calibrates(s);
    cancels(s);
    failsKeepsOld(s);
    resets(s);
    scene_free(s);
    return checksDone("m4_eyecal");
}
