// Moonlight XR Native, tests/m4: the Accessibility tab (lane a11y-tab), driven through the input
// pass's seams with no controllers in use: a press on each of its cells is applied and handed back
// to be stored; the pointer row shows Eyes only where eye gaze arrives; looking at the tab, with the
// head or the eyes, never changes a setting by itself; with the head pointing and a dwell picked,
// the head alone turns it off again; and the Eyes pointer follows the gaze, falling back to the
// head's ray while the eyes are lost. GPLv3.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "scene.h"
#include "xr_pad_bridge.h"

#define FAKE(n) ((uintptr_t)(n))
#define VIEW ((XrSpace)FAKE(0x503))
#define LOCAL ((XrSpace)FAKE(0x504))
#define GAZE ((XrSpace)FAKE(0x505))
#define FRAME_NS 11111111LL

// Where the head and the eyes look (directions in the local space, from the origin), and whether the
// eyes are tracked
static Vec3 g_head = { 0.0f, 0.0f, -1.0f };
static Vec3 g_eyes = { 0.0f, 0.0f, -1.0f };
static int g_eyesTracked;

static XrQuaternionf lookAlong(Vec3 d) {
    Vec3 up = { 0.0f, 1.0f, 0.0f };
    Vec3 right = { 1.0f, 0.0f, 0.0f };
    float yaw = atan2f(-d.x, -d.z);
    float pitch = atan2f(d.y, sqrtf(d.x * d.x + d.z * d.z));
    return quatNorm(quatMul(axisAngleQuat(up, yaw), axisAngleQuat(right, pitch)));
}

static XrResult fakeLocate(XrCtx* ctx, XrSpace space, XrSpace base, XrTime time, XrSpaceLocation* out) {
    (void)ctx; (void)base; (void)time;
    const XrSpaceLocationFlags all = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT
            | XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
    out->pose.position = (XrVector3f){ 0.0f, 0.0f, 0.0f };
    if (space == VIEW) {
        out->locationFlags = all;
        out->pose.orientation = lookAlong(g_head);
        return XR_SUCCESS;
    }
    if (space == GAZE) {
        out->locationFlags = g_eyesTracked ? all : 0;
        out->pose.orientation = lookAlong(g_eyes);
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

static void setUp(Scene* s, int eyes) {
    XrCtx* c = s->ctx;
    scene_reset(s);
    float place[10] = { 0.0f, 0.0f, -2.5f, 0.0f, 0.0f, 0.0f, 1.0f, 2.4f, 2.5f, -1.0f };
    xrSetScreenPose(c, place);
    c->viewSpace = VIEW;
    c->localSpace = LOCAL;
    c->aimSpaces[0] = XR_NULL_HANDLE;
    c->aimSpaces[1] = XR_NULL_HANDLE;
    c->profileKind[0] = c->profileKind[1] = PROFILE_NONE;
    c->locateSpace = fakeLocate;
    c->poseActive = fakePoseActive;
    c->readFloat = fakeFloat;
    c->readBool = fakeBool;
    c->readVec2 = fakeVec2;
    c->pointerSleepOn = 0;
    c->pointerAwake = 1;
    c->padMode = 0;
    // the eye gaze as a session that asked for it and got its binding has it
    c->eyeGaze = eyes;
    c->gazeOffered = eyes;
    c->gazeAsked = eyes;
    c->gazeBound = eyes;
    c->gazeSupportFlag = eyes;
    c->gazeSeen = 0;
    c->gazeReported = 0;
    c->gazeAction = eyes ? (XrAction)FAKE(0x601) : XR_NULL_HANDLE;
    c->aimSpaces[SRC_GAZE] = eyes ? GAZE : XR_NULL_HANDLE;
    g_eyesTracked = 0;
    xrSetPointerSource(c, POINTER_SOURCE_CONTROLLER, 0);
    xrSetHeadLook(c, 0, HEAD_AIM_SENSITIVITY_DEFAULT);
    xrSetMouseMode(c, MOUSE_MODE_ABSOLUTE);
    // the cog open on the Accessibility tab
    c->cogOpen = 1;
    c->cogTab = COG_TAB_ACCESS;
    c->cogPose = cogPanelPose(c, &c->cogW, &c->cogH);
}

// The direction from the origin to the middle of a cell of the tab
static Vec3 cellAt(XrCtx* c, int row, int cell) {
    int n = cogAccessCells(c, row);
    float u = COG_TRACK_L + (cell + 0.5f) * (COG_TRACK_R - COG_TRACK_L) / (float)n;
    float v = cogRowV(COG_TAB_ACCESS, row);
    Vec3 local = { (u - 0.5f) * c->cogW, (0.5f - v) * c->cogH, 0.0f };
    Vec3 off = quatRotate(c->cogPose.orientation, local);
    Vec3 p = { c->cogPose.position.x + off.x, c->cogPose.position.y + off.y, c->cogPose.position.z + off.z };
    return p;
}

// Frames of the pointer and the open panel; how many handed a setting back
static int g_settingsOut;
static int g_lastSetting = -1, g_lastValue = -1;

static void frames(XrCtx* c, int n) {
    float out[IN_SLOTS];
    for (int i = 0; i < n; i++) {
        g_now += FRAME_NS;
        c->predictedDisplayTime = (XrTime)g_now;
        xrPointerPanelStep(c, g_now, 0, out);
        if (out[IN_SETTING] >= 0.0f) {
            g_settingsOut++;
            g_lastSetting = (int)out[IN_SETTING];
            g_lastValue = (int)out[IN_SETTING_VALUE];
        }
    }
}

// Each cell pressed hands back its setting, which lands in config.ini's key
static void presses(Scene* s) {
    XrCtx* c = s->ctx;
    setUp(s, 0);
    Settings st;
    settings_defaults(&st);
    float out[IN_SLOTS];
    // no eye gaze: the pointer row is Controller and Head only, a press past them does nothing
    CHECK(cogAccessCells(c, COG_ACCESS_ROW_POINTER) == 2);
    CHECK(cogRowCells(COG_TAB_ACCESS, COG_ACCESS_ROW_POINTER) == 3);
    out[IN_SETTING] = -1.0f;
    cogApplyCell(c, COG_TAB_ACCESS, COG_ACCESS_ROW_POINTER, 2, out);
    CHECK(out[IN_SETTING] == -1.0f && c->pointerSource == POINTER_SOURCE_CONTROLLER);
    // the dwell row is dead while the controllers point
    CHECK(!cogRowLive(c, COG_TAB_ACCESS, COG_ACCESS_ROW_DWELL));
    cogApplyCell(c, COG_TAB_ACCESS, COG_ACCESS_ROW_DWELL, 2, out);
    CHECK(out[IN_SETTING] == -1.0f && c->headDwellMs == 0);
    // Head
    cogApplyCell(c, COG_TAB_ACCESS, COG_ACCESS_ROW_POINTER, 1, out);
    CHECK((int)out[IN_SETTING] == SETTING_POINTER_SOURCE && (int)out[IN_SETTING_VALUE] == POINTER_SOURCE_HEAD);
    CHECK(c->pointerSource == POINTER_SOURCE_HEAD);
    CHECK(xrStoreSetting(c, &st, (int)out[IN_SETTING], (int)out[IN_SETTING_VALUE]) && st.pointer_source == POINTER_SOURCE_HEAD);
    CHECK(cogRowLive(c, COG_TAB_ACCESS, COG_ACCESS_ROW_DWELL));
    // each dwell time
    for (int cell = 0; cell < COG_ACCESS_DWELL_CELLS; cell++) {
        out[IN_SETTING] = -1.0f;
        cogApplyCell(c, COG_TAB_ACCESS, COG_ACCESS_ROW_DWELL, cell, out);
        CHECK((int)out[IN_SETTING] == SETTING_HEAD_DWELL && (int)out[IN_SETTING_VALUE] == accessDwellMs(cell));
        CHECK(c->headDwellMs == accessDwellMs(cell) && c->pointerSource == POINTER_SOURCE_HEAD);
        xrStoreSetting(c, &st, (int)out[IN_SETTING], (int)out[IN_SETTING_VALUE]);
        CHECK(st.head_dwell_ms == accessDwellMs(cell) && accessDwellCell(st.head_dwell_ms) == cell);
    }
    // head-look, Back as Guide, the mouse mode
    cogApplyCell(c, COG_TAB_ACCESS, COG_ACCESS_ROW_HEAD_LOOK, 1, out);
    CHECK((int)out[IN_SETTING] == SETTING_HEAD_LOOK && c->headLookOn == 1);
    xrStoreSetting(c, &st, (int)out[IN_SETTING], (int)out[IN_SETTING_VALUE]);
    cogApplyCell(c, COG_TAB_ACCESS, COG_ACCESS_ROW_BACK_GUIDE, 1, out);
    CHECK((int)out[IN_SETTING] == SETTING_BACK_AS_GUIDE && (int)out[IN_SETTING_VALUE] == 1);
    xrStoreSetting(c, &st, (int)out[IN_SETTING], (int)out[IN_SETTING_VALUE]);
    cogApplyCell(c, COG_TAB_ACCESS, COG_ACCESS_ROW_MOUSE, 1, out);
    CHECK((int)out[IN_SETTING] == SETTING_MOUSE_MODE && c->mouseMode == MOUSE_MODE_RELATIVE);
    xrStoreSetting(c, &st, (int)out[IN_SETTING], (int)out[IN_SETTING_VALUE]);
    CHECK(st.head_look == 1 && st.back_as_guide == 1 && st.mouse_mode == MOUSE_MODE_RELATIVE);
    // and back
    cogApplyCell(c, COG_TAB_ACCESS, COG_ACCESS_ROW_BACK_GUIDE, 0, out);
    xrStoreSetting(c, &st, (int)out[IN_SETTING], (int)out[IN_SETTING_VALUE]);
    cogApplyCell(c, COG_TAB_ACCESS, COG_ACCESS_ROW_POINTER, 0, out);
    xrStoreSetting(c, &st, (int)out[IN_SETTING], (int)out[IN_SETTING_VALUE]);
    CHECK(st.back_as_guide == 0 && st.pointer_source == POINTER_SOURCE_CONTROLLER && c->pointerSource == POINTER_SOURCE_CONTROLLER);
    // the stored values survive config.ini
    char* text = settings_to_text(&st);
    Settings back;
    settings_defaults(&back);
    settings_from_text(&back, text ? text : "", NULL);
    CHECK(back.head_dwell_ms == 1500 && back.head_look == 1 && back.mouse_mode == MOUSE_MODE_RELATIVE);
    free(text);
    xrPadBridgeSetBackAsGuide(0);
}

// Looking at the tab, with the head and the eyes, for a long time, on every cell: nothing changes.
// Only a press does, and with the controllers pointing (the default) the head and the eyes press
// nothing.
static void neverByItself(Scene* s) {
    XrCtx* c = s->ctx;
    setUp(s, 1);
    g_eyesTracked = 1;
    g_settingsOut = 0;
    // gaze = true as well (the eyes point, the hands would pinch to click)
    c->gazeEnabled = 1;
    for (int row = 0; row < COG_ACCESS_ROW_COUNT; row++) {
        for (int cell = 0; cell < cogAccessCells(c, row); cell++) {
            g_head = cellAt(c, row, cell);
            g_eyes = cellAt(c, (row + 1) % COG_ACCESS_ROW_COUNT, 1);
            frames(c, 270); // 3 s on each
            CHECK(c->pointerSource == POINTER_SOURCE_CONTROLLER && c->headDwellMs == 0);
            CHECK(c->headLookOn == 0 && c->mouseMode == MOUSE_MODE_ABSOLUTE);
            CHECK(c->dwellClicks == 0 && g_settingsOut == 0);
        }
    }
    // the ray did reach the tab: the cell under the head is lit
    c->gazeEnabled = 0;
    // the eyes were seen, so the Eyes cell is offered now, and still nothing is on
    CHECK(c->gazeSeen == 1 && xrAccessEyes(c) == ACCESS_EYES_READY);
    CHECK(cogAccessCells(c, COG_ACCESS_ROW_POINTER) == 3);
    CHECK(c->pointerSource == POINTER_SOURCE_CONTROLLER);
}

// The head picked and a dwell time: resting the head on Controller for longer than the dwell turns
// head pointing off, with nothing but the head
static void headTurnsItOff(Scene* s) {
    XrCtx* c = s->ctx;
    setUp(s, 0);
    float out[IN_SLOTS];
    cogApplyCell(c, COG_TAB_ACCESS, COG_ACCESS_ROW_POINTER, POINTER_SOURCE_HEAD, out);
    // the head pointing with the dwell off: resting on a cell clicks nothing
    g_settingsOut = 0;
    g_head = cellAt(c, COG_ACCESS_ROW_HEAD_LOOK, 1);
    frames(c, 270);
    CHECK(c->cogHoverSlider == COG_ACCESS_ROW_HEAD_LOOK && c->cogHoverCell == 1);
    CHECK(g_settingsOut == 0 && c->headLookOn == 0 && c->dwellClicks == 0);
    cogApplyCell(c, COG_TAB_ACCESS, COG_ACCESS_ROW_DWELL, 2, out);
    CHECK(c->pointerSource == POINTER_SOURCE_HEAD && c->headDwellMs == 900);
    // a glance across Controller is not a click
    g_head = cellAt(c, COG_ACCESS_ROW_POINTER, 0);
    frames(c, 60); // 0.67 s
    g_head = cellAt(c, COG_ACCESS_ROW_HEAD_LOOK, 0);
    frames(c, 10);
    CHECK(c->pointerSource == POINTER_SOURCE_HEAD);
    // resting on it is
    g_head = cellAt(c, COG_ACCESS_ROW_POINTER, 0);
    g_settingsOut = 0;
    frames(c, 120); // 1.3 s
    CHECK(c->dwellClicks >= 1);
    CHECK(c->pointerSource == POINTER_SOURCE_CONTROLLER);
    // handed back to be stored, once
    CHECK(g_settingsOut == 1 && g_lastSetting == SETTING_POINTER_SOURCE && g_lastValue == POINTER_SOURCE_CONTROLLER);
    // the dwell time is kept for the next time the head points
    CHECK(c->headDwellMs == 900);
    // a dwell on a cell clicks it once: resting there on, nothing more is handed back
    cogApplyCell(c, COG_TAB_ACCESS, COG_ACCESS_ROW_POINTER, POINTER_SOURCE_HEAD, out);
    g_head = cellAt(c, COG_ACCESS_ROW_DWELL, 4);
    g_settingsOut = 0;
    frames(c, 450); // 5 s
    CHECK(c->headDwellMs == 1500 && c->dwellClicks == 2);
    CHECK(g_settingsOut == 1 && g_lastSetting == SETTING_HEAD_DWELL && g_lastValue == 1500);
    xrSetPointerSource(c, POINTER_SOURCE_CONTROLLER, 0);
}

// Eyes: the dwell lands where the eyes look while they are tracked; lost for longer than 200 ms,
// the head's ray points instead
static void eyesFallBack(Scene* s) {
    XrCtx* c = s->ctx;
    setUp(s, 1);
    g_eyesTracked = 1;
    g_head = cellAt(c, COG_ACCESS_ROW_HEAD_LOOK, 1);   // head-look On
    g_eyes = cellAt(c, COG_ACCESS_ROW_MOUSE, 1);       // Relative
    frames(c, 5);
    CHECK(c->gazeSeen == 1 && cogAccessCells(c, COG_ACCESS_ROW_POINTER) == 3);
    float out[IN_SLOTS];
    out[IN_SETTING] = -1.0f;
    cogApplyCell(c, COG_TAB_ACCESS, COG_ACCESS_ROW_POINTER, POINTER_SOURCE_GAZE, out);
    CHECK((int)out[IN_SETTING] == SETTING_POINTER_SOURCE && (int)out[IN_SETTING_VALUE] == POINTER_SOURCE_GAZE);
    cogApplyCell(c, COG_TAB_ACCESS, COG_ACCESS_ROW_DWELL, 2, out);
    CHECK(c->pointerSource == POINTER_SOURCE_GAZE && c->headDwellMs == 900);
    // the eyes tracked: after their 300 ms the dot is theirs, and their dwell picks Relative
    frames(c, 150);
    CHECK(c->gazeFall.head == 0);
    CHECK(c->mouseMode == MOUSE_MODE_RELATIVE);
    CHECK(c->headLookOn == 0);
    // a blink (150 ms) leaves the eyes pointing
    g_eyesTracked = 0;
    frames(c, 13);
    CHECK(c->gazeFall.head == 0);
    g_eyesTracked = 1;
    frames(c, 5);
    // lost: the head's ray takes over and its dwell picks head-look On
    g_eyesTracked = 0;
    frames(c, 25);
    CHECK(c->gazeFall.head == 1);
    frames(c, 130);
    CHECK(c->headLookOn == 1);
    xrSetHeadLook(c, 0, HEAD_AIM_SENSITIVITY_DEFAULT);
    xrSetMouseMode(c, MOUSE_MODE_ABSOLUTE);
    xrSetPointerSource(c, POINTER_SOURCE_CONTROLLER, 0);
}

// The eye tracking line: what the session found, and the gaze arriving
static void eyesLine(Scene* s) {
    XrCtx* c = s->ctx;
    setUp(s, 0);
    c->gazeOffered = 0;
    CHECK(xrAccessEyes(c) == ACCESS_EYES_UNAVAILABLE);
    c->gazeOffered = 1;
    CHECK(xrAccessEyes(c) == ACCESS_EYES_NOT_ASKED);
    setUp(s, 1);
    c->gazeSupportFlag = 0;   // SteamVR said no, the action is still watched
    CHECK(xrAccessEyes(c) == ACCESS_EYES_UNAVAILABLE);
    c->gazeSupportFlag = 1;
    CHECK(xrAccessEyes(c) == ACCESS_EYES_WAITING);
    // focused for 10 s with no gaze: logged once, and still waiting
    frames(c, 950);
    CHECK(c->gazeReported == 1 && c->gazeSeen == 0 && xrAccessEyes(c) == ACCESS_EYES_WAITING);
    g_eyesTracked = 1;
    frames(c, 2);
    CHECK(c->gazeSeen == 1 && xrAccessEyes(c) == ACCESS_EYES_READY);
    // the panel's view says the same
    scene_frame(s, NULL);
    const UiView* v = xrPanelsLastView(c);
    CHECK(v != NULL && v->access_eyes == ACCESS_EYES_READY && v->cog_tab == COG_TAB_ACCESS);
}

int main(void) {
    Scene* s = scene_new();
    presses(s);
    neverByItself(s);
    headTurnsItOff(s);
    eyesFallBack(s);
    eyesLine(s);
    scene_free(s);
    return checksDone("m4_access");
}
