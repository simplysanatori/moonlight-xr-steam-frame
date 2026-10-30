// Moonlight XR Native, tests/m4: the Accessibility tab (lanes a11y-tab, a11y-2), driven through
// the input pass's seams with no controllers in use: a press on each of its cells is applied and
// handed back to be stored; the pointer row always has Eyes, dimmed only where the runtime offers no
// eye gaze; looking at the tab, with the head or the eyes, never changes a setting by itself; with
// the head pointing and a dwell picked, the head alone turns it off again; the Eyes pointer follows
// the gaze, falling back to the head's ray while the eyes are lost; the Dot size track scales the
// dot and its dwell ring; and the footer follows the row under the pointer. GPLv3.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#include <linux/input.h>

#include "check.h"
#include "scene.h"

#define FAKE(n) ((uintptr_t)(n))
#define VIEW ((XrSpace)FAKE(0x503))
#define LOCAL ((XrSpace)FAKE(0x504))
#define GAZE ((XrSpace)FAKE(0x505))
#define FRAME_NS 11111111LL
// Where the rows' labels are, left of the cells
#define LABEL_U 0.12f

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
    // the eye gaze as a session on a runtime that offers it has it (lane a11y-2: always asked for)
    c->eyeGaze = eyes;
    c->gazeOffered = eyes;
    c->gazeAsked = 0;
    c->gazeBound = eyes;
    c->gazeSupportFlag = eyes;
    c->gazeSeen = 0;
    c->gazeReported = 0;
    c->gazeAction = eyes ? (XrAction)FAKE(0x601) : XR_NULL_HANDLE;
    c->aimSpaces[SRC_GAZE] = eyes ? GAZE : XR_NULL_HANDLE;
    g_eyesTracked = 0;
    c->gazeEnabled = 0;
    xrSetPointerSource(c, POINTER_SOURCE_CONTROLLER, 0);
    c->assistDotSize = ACCESS_DOT_DEFAULT;
    c->cogDragSlider = -1;
    // the cog open on the Accessibility tab
    c->cogOpen = 1;
    c->cogTab = COG_TAB_ACCESS;
    c->cogPose = cogPanelPose(c, &c->cogW, &c->cogH);
}

// A point of the panel (u, v) in the local space
static Vec3 panelAt(XrCtx* c, float u, float v) {
    Vec3 local = { (u - 0.5f) * c->cogW, (0.5f - v) * c->cogH, 0.0f };
    Vec3 off = quatRotate(c->cogPose.orientation, local);
    Vec3 p = { c->cogPose.position.x + off.x, c->cogPose.position.y + off.y, c->cogPose.position.z + off.z };
    return p;
}

// The middle of a cell of the tab
static Vec3 cellAt(XrCtx* c, int row, int cell) {
    int n = cogAccessCells(c, row);
    return panelAt(c, COG_TRACK_L + (cell + 0.5f) * (COG_TRACK_R - COG_TRACK_L) / (float)n, cogRowV(COG_TAB_ACCESS, row));
}

// The Dot size track's step buttons (dir -1 the smaller, 1 the larger) and a row's label
static Vec3 stepAt(XrCtx* c, int dir) {
    float u = dir < 0 ? COG_TRACK_L + COG_CHEVRON_W * 0.5f : COG_TRACK_R - COG_CHEVRON_W * 0.5f;
    return panelAt(c, u, cogRowV(COG_TAB_ACCESS, COG_ACCESS_ROW_DOT));
}

static Vec3 labelAt(XrCtx* c, int row) {
    return panelAt(c, LABEL_U, cogRowV(COG_TAB_ACCESS, row));
}

// Frames of the pointer and the open panel; how many handed a setting back
static int g_settingsOut;
static int g_lastSetting = -1, g_lastValue = -1;

static void frames(XrCtx* c, int n) {
    float out[IN_SLOTS];
    for (int i = 0; i < n; i++) {
        g_now += FRAME_NS;
        c->predictedDisplayTime = (XrTime)g_now;
        xrAssistFrame(c, g_now);   // as the session does before the input pass (lane a11y-2)
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
    // no eye gaze offered: the pointer row still has Eyes, dimmed, and a press on it does nothing
    CHECK(cogAccessCells(c, COG_ACCESS_ROW_POINTER) == 3 && cogRowCells(COG_TAB_ACCESS, COG_ACCESS_ROW_POINTER) == 3);
    CHECK(xrAccessEyes(c) == ACCESS_EYES_UNAVAILABLE && !cogAccessCellLive(c, COG_ACCESS_ROW_POINTER, 2));
    out[IN_SETTING] = -1.0f;
    cogApplyCell(c, COG_TAB_ACCESS, COG_ACCESS_ROW_POINTER, 2, out);
    CHECK(out[IN_SETTING] == -1.0f && c->pointerSource == POINTER_SOURCE_CONTROLLER);
    // the dwell row and the dot track are dead while the controllers point
    CHECK(!cogRowLive(c, COG_TAB_ACCESS, COG_ACCESS_ROW_DWELL) && !cogRowLive(c, COG_TAB_ACCESS, COG_ACCESS_ROW_DOT));
    cogApplyCell(c, COG_TAB_ACCESS, COG_ACCESS_ROW_DWELL, 2, out);
    CHECK(out[IN_SETTING] == -1.0f && c->headDwellMs == 0);
    cogStepTrack(c, COG_TAB_ACCESS, COG_ACCESS_ROW_DOT, 1, out);
    CHECK(out[IN_SETTING] == -1.0f || (int)out[IN_SETTING] != SETTING_ASSIST_DOT_SIZE || c->assistDotSize == ACCESS_DOT_DEFAULT);
    CHECK(c->assistDotSize == ACCESS_DOT_DEFAULT);
    // Head
    out[IN_SETTING] = -1.0f;
    cogApplyCell(c, COG_TAB_ACCESS, COG_ACCESS_ROW_POINTER, 1, out);
    CHECK((int)out[IN_SETTING] == SETTING_POINTER_SOURCE && (int)out[IN_SETTING_VALUE] == POINTER_SOURCE_HEAD);
    CHECK(c->pointerSource == POINTER_SOURCE_HEAD);
    CHECK(xrStoreSetting(c, &st, (int)out[IN_SETTING], (int)out[IN_SETTING_VALUE]) && st.pointer_source == POINTER_SOURCE_HEAD);
    CHECK(cogRowLive(c, COG_TAB_ACCESS, COG_ACCESS_ROW_DWELL) && cogRowLive(c, COG_TAB_ACCESS, COG_ACCESS_ROW_DOT));
    // each dwell time
    for (int cell = 0; cell < COG_ACCESS_DWELL_CELLS; cell++) {
        out[IN_SETTING] = -1.0f;
        cogApplyCell(c, COG_TAB_ACCESS, COG_ACCESS_ROW_DWELL, cell, out);
        CHECK((int)out[IN_SETTING] == SETTING_HEAD_DWELL && (int)out[IN_SETTING_VALUE] == accessDwellMs(cell));
        CHECK(c->headDwellMs == accessDwellMs(cell) && c->pointerSource == POINTER_SOURCE_HEAD);
        xrStoreSetting(c, &st, (int)out[IN_SETTING], (int)out[IN_SETTING_VALUE]);
        CHECK(st.head_dwell_ms == accessDwellMs(cell) && accessDwellCell(st.head_dwell_ms) == cell);
    }
    // the dot: each step button press is a 25 % step, handed back at once; the ends hold
    CHECK(cogRowIsTrack(COG_TAB_ACCESS, COG_ACCESS_ROW_DOT) && !cogRowIsTrack(COG_TAB_ACCESS, COG_ACCESS_ROW_DWELL));
    CHECK_NEAR(cogSliderValue(c, COG_TAB_ACCESS, COG_ACCESS_ROW_DOT), 0.2f, 1e-5);
    out[IN_SETTING] = -1.0f;
    cogStepTrack(c, COG_TAB_ACCESS, COG_ACCESS_ROW_DOT, 1, out);
    CHECK((int)out[IN_SETTING] == SETTING_ASSIST_DOT_SIZE && (int)out[IN_SETTING_VALUE] == 125 && c->assistDotSize == 125);
    xrStoreSetting(c, &st, (int)out[IN_SETTING], (int)out[IN_SETTING_VALUE]);
    CHECK(st.assist_dot_size == 125);
    for (int i = 0; i < 20; i++) {
        cogStepTrack(c, COG_TAB_ACCESS, COG_ACCESS_ROW_DOT, -1, out);
    }
    CHECK(c->assistDotSize == ACCESS_DOT_MIN && (int)out[IN_SETTING_VALUE] == ACCESS_DOT_MIN);
    // a drag lands on a step, and is handed back when it ends
    cogApplySlider(c, COG_TAB_ACCESS, COG_ACCESS_ROW_DOT, cogRunU(0.62f));
    CHECK(c->assistDotSize == 200);
    c->cogDragSlider = COG_ACCESS_ROW_DOT;
    c->cogDragFace = COG_TAB_ACCESS;
    out[IN_SETTING] = -1.0f;
    cogDragEnded(c, out);
    CHECK((int)out[IN_SETTING] == SETTING_ASSIST_DOT_SIZE && (int)out[IN_SETTING_VALUE] == 200);
    xrStoreSetting(c, &st, (int)out[IN_SETTING], (int)out[IN_SETTING_VALUE]);
    // the panel's view follows the drag (the thumb and the readout are drawn from it)
    Settings live = st;
    cogApplySlider(c, COG_TAB_ACCESS, COG_ACCESS_ROW_DOT, cogRunU(1.0f));
    xrSettingsFromCtx(c, &live);
    CHECK(live.assist_dot_size == ACCESS_DOT_MAX);
    c->assistDotSize = 200;
    // and back to the controllers
    cogApplyCell(c, COG_TAB_ACCESS, COG_ACCESS_ROW_POINTER, 0, out);
    xrStoreSetting(c, &st, (int)out[IN_SETTING], (int)out[IN_SETTING_VALUE]);
    CHECK(st.pointer_source == POINTER_SOURCE_CONTROLLER && c->pointerSource == POINTER_SOURCE_CONTROLLER);
    // the stored values survive config.ini
    char* text = settings_to_text(&st);
    Settings back;
    settings_defaults(&back);
    settings_from_text(&back, text ? text : "", NULL);
    CHECK(back.head_dwell_ms == 1500 && back.assist_dot_size == 200);
    free(text);
    // a runtime that offers eye gaze: Eyes takes a press, before any gaze has arrived
    setUp(s, 1);
    CHECK(xrAccessEyes(c) == ACCESS_EYES_WAITING && cogAccessCellLive(c, COG_ACCESS_ROW_POINTER, 2));
    out[IN_SETTING] = -1.0f;
    cogApplyCell(c, COG_TAB_ACCESS, COG_ACCESS_ROW_POINTER, 2, out);
    CHECK((int)out[IN_SETTING] == SETTING_POINTER_SOURCE && (int)out[IN_SETTING_VALUE] == POINTER_SOURCE_GAZE);
    CHECK(c->pointerSource == POINTER_SOURCE_GAZE);
    xrSetPointerSource(c, POINTER_SOURCE_CONTROLLER, 0);
}

// Looking at the tab, with the head and the eyes, for a long time, on every cell and step button:
// nothing changes. Only a press does, and with the controllers pointing (the default) the head and
// the eyes press nothing.
static void neverByItself(Scene* s) {
    XrCtx* c = s->ctx;
    setUp(s, 1);
    g_eyesTracked = 1;
    g_settingsOut = 0;
    // gaze = true as well (the eyes point, the hands would pinch to click)
    c->gazeEnabled = 1;
    Vec3 spots[16];
    int n = 0;
    for (int row = 0; row < COG_ACCESS_ROW_COUNT; row++) {
        for (int cell = 0; cell < cogAccessCells(c, row); cell++) {
            spots[n++] = cellAt(c, row, cell);
        }
    }
    spots[n++] = stepAt(c, -1);
    spots[n++] = stepAt(c, 1);
    for (int i = 0; i < n; i++) {
        g_head = spots[i];
        g_eyes = spots[(i + 3) % n];
        frames(c, 270); // 3 s on each
        CHECK(c->pointerSource == POINTER_SOURCE_CONTROLLER && c->headDwellMs == 0);
        CHECK(c->assistDotSize == ACCESS_DOT_DEFAULT);
        CHECK(c->dwellClicks == 0 && g_settingsOut == 0);
    }
    c->gazeEnabled = 0;
    // the eyes were seen: the line says so, and still nothing is on
    CHECK(c->gazeSeen == 1 && xrAccessEyes(c) == ACCESS_EYES_READY);
    CHECK(c->pointerSource == POINTER_SOURCE_CONTROLLER);
    // gaze = false and the controllers pointing (the defaults): the gaze is read for the eye line
    // only, and points at nothing
    setUp(s, 1);
    g_eyesTracked = 1;
    g_eyes = cellAt(c, COG_ACCESS_ROW_POINTER, 1);
    g_head = cellAt(c, COG_ACCESS_ROW_POINTER, 2);
    frames(c, 200);
    CHECK(c->gazeSeen == 1 && c->cogHoverSlider == -1 && g_settingsOut == 0 && c->dwellClicks == 0);
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
    g_head = cellAt(c, COG_ACCESS_ROW_DWELL, 3);
    frames(c, 270);
    CHECK(c->cogHoverSlider == COG_ACCESS_ROW_DWELL && c->cogHoverCell == 3);
    CHECK(g_settingsOut == 0 && c->headDwellMs == 0 && c->dwellClicks == 0);
    cogApplyCell(c, COG_TAB_ACCESS, COG_ACCESS_ROW_DWELL, 2, out);
    CHECK(c->pointerSource == POINTER_SOURCE_HEAD && c->headDwellMs == 900);
    // a glance across Controller is not a click
    g_head = cellAt(c, COG_ACCESS_ROW_POINTER, 0);
    frames(c, 60); // 0.67 s
    g_head = labelAt(c, COG_ACCESS_ROW_DOT);
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
    // a dwell on the Dot size's larger step button: one 25 % step, handed back
    g_head = stepAt(c, 1);
    g_settingsOut = 0;
    frames(c, 300);
    CHECK(c->assistDotSize == 125 && g_settingsOut == 1 && g_lastSetting == SETTING_ASSIST_DOT_SIZE && g_lastValue == 125);
    xrSetPointerSource(c, POINTER_SOURCE_CONTROLLER, 0);
}

// Eyes: the dwell lands where the eyes look while they are tracked; lost for longer than 200 ms,
// the head's ray points instead
static void eyesFallBack(Scene* s) {
    XrCtx* c = s->ctx;
    setUp(s, 1);
    g_eyesTracked = 1;
    g_head = stepAt(c, -1);   // the dot smaller
    g_eyes = stepAt(c, 1);    // the dot larger
    frames(c, 5);
    CHECK(c->gazeSeen == 1);
    float out[IN_SLOTS];
    out[IN_SETTING] = -1.0f;
    cogApplyCell(c, COG_TAB_ACCESS, COG_ACCESS_ROW_POINTER, POINTER_SOURCE_GAZE, out);
    CHECK((int)out[IN_SETTING] == SETTING_POINTER_SOURCE && (int)out[IN_SETTING_VALUE] == POINTER_SOURCE_GAZE);
    cogApplyCell(c, COG_TAB_ACCESS, COG_ACCESS_ROW_DWELL, 2, out);
    CHECK(c->pointerSource == POINTER_SOURCE_GAZE && c->headDwellMs == 900);
    // the eyes tracked: after their 300 ms the dot is theirs, and their dwell makes it larger
    frames(c, 150);
    CHECK(c->gazeFall.head == 0);
    CHECK(c->assistDotSize == 125);
    // a blink (150 ms) leaves the eyes pointing
    g_eyesTracked = 0;
    frames(c, 13);
    CHECK(c->gazeFall.head == 0);
    g_eyesTracked = 1;
    frames(c, 5);
    // lost: the head's ray takes over and its dwell makes it smaller again
    g_eyesTracked = 0;
    frames(c, 25);
    CHECK(c->gazeFall.head == 1);
    frames(c, 130);
    CHECK(c->assistDotSize == 100);
    xrSetPointerSource(c, POINTER_SOURCE_CONTROLLER, 0);
}

// The eye tracking line: what the session found, and the gaze arriving
static void eyesLine(Scene* s) {
    XrCtx* c = s->ctx;
    setUp(s, 0);
    c->gazeOffered = 0;
    CHECK(xrAccessEyes(c) == ACCESS_EYES_UNAVAILABLE);
    // offered, but the action could not be set up
    c->gazeOffered = 1;
    CHECK(xrAccessEyes(c) == ACCESS_EYES_NO_ACTION && cogAccessCellLive(c, COG_ACCESS_ROW_POINTER, 2));
    setUp(s, 1);
    c->gazeSupportFlag = 0;   // SteamVR's flag plays no part
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

// The footer: the row under the pointer, its label or its cells, live or greyed
static void footer(Scene* s) {
    XrCtx* c = s->ctx;
    setUp(s, 0);
    xrSetPointerSource(c, POINTER_SOURCE_HEAD, 0);
    static const int ROWS[] = { COG_ACCESS_ROW_DOT, COG_ACCESS_ROW_POINTER, COG_ACCESS_ROW_DWELL };
    for (int i = 0; i < 3; i++) {
        g_head = labelAt(c, ROWS[i]);
        frames(c, 3);
        CHECK(xrAccessHoverRow(c) == ROWS[i]);
        scene_frame(s, NULL);
        xrPanelsFlush(c);
        const UiView* v = xrPanelsLastView(c);
        CHECK(v != NULL && v->access_row == ROWS[i]);
    }
    g_head = cellAt(c, COG_ACCESS_ROW_DWELL, 1);
    frames(c, 3);
    CHECK(xrAccessHoverRow(c) == COG_ACCESS_ROW_DWELL);
    // the status line and the instructions are no row
    g_head = panelAt(c, 0.5f, COG_ACCESS_HELP_T);
    frames(c, 3);
    CHECK(xrAccessHoverRow(c) == -1);
    // a greyed row is described too: the controllers' pointer greys the dwell and the dot (here
    // the eyes point for them, gaze = true)
    setUp(s, 1);
    c->gazeEnabled = 1;
    g_eyesTracked = 1;
    g_eyes = labelAt(c, COG_ACCESS_ROW_DOT);
    frames(c, 30);
    CHECK(!cogRowLive(c, COG_TAB_ACCESS, COG_ACCESS_ROW_DOT));
    CHECK(xrAccessHoverRow(c) == COG_ACCESS_ROW_DOT);
    g_eyes = cellAt(c, COG_ACCESS_ROW_DWELL, 2);
    frames(c, 3);
    CHECK(xrAccessHoverRow(c) == COG_ACCESS_ROW_DWELL && c->cogHoverSlider == -1);
    c->gazeEnabled = 0;
    // another tab, or the cog shut: none
    c->cogTab = COG_TAB_ABOUT;
    CHECK(xrAccessHoverRow(c) == -1);
    c->cogTab = COG_TAB_ACCESS;
    c->cogOpen = 0;
    CHECK(xrAccessHoverRow(c) == -1);
    xrSetPointerSource(c, POINTER_SOURCE_CONTROLLER, 0);
}

static const XrCompositionLayerQuad* findQuad(Scene* s, int slot) {
    UiRectPx r = xrAtlasRect(slot);
    for (uint32_t i = 0; i < s->out.count; i++) {
        const XrCompositionLayerBaseHeader* b = s->out.order[i];
        if (b->type != XR_TYPE_COMPOSITION_LAYER_QUAD) continue;
        const XrCompositionLayerQuad* q = (const XrCompositionLayerQuad*)b;
        if (q->subImage.imageRect.offset.x == r.x && q->subImage.imageRect.offset.y == r.y &&
            q->subImage.imageRect.extent.width == r.w && q->subImage.imageRect.extent.height == r.h) {
            return q;
        }
    }
    return NULL;
}

// The dot's side as the frame draws it, at the slot (the dot, or the ring while a dwell charges)
static float dotSide(Scene* s, int slot) {
    Vec3 hand = { 0.0f, 0.0f, 0.0f };
    Vec3 target = { 0.0f, 0.0f, -2.0f };
    scene_ray_to(s, hand, target);
    scene_frame(s, NULL);
    const XrCompositionLayerQuad* q = findQuad(s, slot);
    return q != NULL ? q->size.width : -1.0f;
}

// Dot size scales the head and eye pointer's dot and its dwell ring, and only theirs
static void dotScales(Scene* s) {
    XrCtx* c = s->ctx;
    scene_reset(s);
    c->dwellFraction = -1.0f;
    xrSetPointerSource(c, POINTER_SOURCE_HEAD, 900);
    c->assistDotSize = 100;
    float base = dotSide(s, ATLAS_DOT);
    CHECK(base > 0.0f);
    c->assistDotSize = 200;
    CHECK_NEAR(dotSide(s, ATLAS_DOT), base * 2.0f, 1e-5);
    c->assistDotSize = 50;
    CHECK_NEAR(dotSide(s, ATLAS_DOT), base * 0.5f, 1e-5);
    c->assistDotSize = 300;
    CHECK_NEAR(dotSide(s, ATLAS_DOT), base * 3.0f, 1e-5);
    // the ring closing in (half way: twice the dot) scales with it
    c->dwellFraction = 0.5f;
    c->assistDotSize = 100;
    float ring = dotSide(s, ATLAS_OUTLINE);
    CHECK_NEAR(ring, base * dwellRingScale(0.5f), 1e-5);
    c->assistDotSize = 250;
    CHECK_NEAR(dotSide(s, ATLAS_OUTLINE), ring * 2.5f, 1e-5);
    // the eyes too; the controllers' dot is today's whatever the setting
    c->dwellFraction = -1.0f;
    xrSetPointerSource(c, POINTER_SOURCE_GAZE, 900);
    CHECK_NEAR(dotSide(s, ATLAS_DOT), base * 2.5f, 1e-5);
    xrSetPointerSource(c, POINTER_SOURCE_CONTROLLER, 0);
    CHECK_NEAR(dotSide(s, ATLAS_DOT), base, 1e-5);
    c->assistDotSize = ACCESS_DOT_DEFAULT;
    c->beamVisible = 0;
}

// Lane a11y-2: the headset button's clicks land where the dot is: on our menus as a press, on the
// picture as the mouse buttons; a drag holds the left button
static void assistClicks(Scene* s) {
    XrCtx* c = s->ctx;
    setUp(s, 0);
    float out[IN_SLOTS];
    cogApplyCell(c, COG_TAB_ACCESS, COG_ACCESS_ROW_POINTER, POINTER_SOURCE_HEAD, out);
    g_head = cellAt(c, COG_ACCESS_ROW_DWELL, 3);
    frames(c, 20);
    g_settingsOut = 0;
    xrAssistAction(c, ASSIST_LEFT, g_now);
    CHECK(c->assistUi == 1);
    frames(c, 10);
    CHECK(c->headDwellMs == 1200 && g_settingsOut == 1 && g_lastSetting == SETTING_HEAD_DWELL);
    // a right click on our menus is one press
    g_head = cellAt(c, COG_ACCESS_ROW_DWELL, 1);
    frames(c, 20);
    xrAssistAction(c, ASSIST_RIGHT, g_now);
    frames(c, 10);
    CHECK(c->headDwellMs == 600);
    // on the picture: the cog shut, the dot on the screen
    c->cogOpen = 0;
    g_head = (Vec3){ 0.0f, 0.0f, -1.0f };
    frames(c, 20);
    CHECK(c->hoverKind == HOVER_SCREEN);
    int sawLeft = 0, sawRight = 0;
    xrAssistAction(c, ASSIST_RIGHT, g_now);
    CHECK(c->assistUi == 0);
    for (int i = 0; i < 12; i++) {
        g_now += FRAME_NS;
        c->predictedDisplayTime = (XrTime)g_now;
        xrAssistFrame(c, g_now);
        int b = xrButtonsFrame(c, 0, HOVER_SCREEN, 1);   // the buttons part of the frame, on the picture
        sawRight |= (b & VR_BUTTON_RIGHT) != 0;
        sawLeft |= (b & VR_BUTTON_LEFT) != 0;
    }
    CHECK(sawRight && !sawLeft);
    // a drag: the left button stays down until it is let go
    xrAssistAction(c, ASSIST_DRAG_DOWN, g_now);
    int held = 0;
    for (int i = 0; i < 60; i++) {
        g_now += FRAME_NS;
        c->predictedDisplayTime = (XrTime)g_now;
        xrAssistFrame(c, g_now);
        held += (xrButtonsFrame(c, 0, HOVER_SCREEN, 1) & VR_BUTTON_LEFT) != 0;
    }
    CHECK(held == 60);
    xrAssistAction(c, ASSIST_DRAG_UP, g_now);
    xrAssistFrame(c, g_now);
    CHECK(c->assistButtons == 0 && (xrButtonsFrame(c, 0, HOVER_SCREEN, 1) & VR_BUTTON_LEFT) == 0);
    c->cogOpen = 1;
    xrSetPointerSource(c, POINTER_SOURCE_CONTROLLER, 0);
}

// Lane a11y-2: the headset button's device through xrAuxButtonFrame, a FIFO standing in for it: taken
// (grabbed) only while on, focused and streaming with the head pointing; given back on focus loss,
// with the setting off, and for 15 s after a 3 s hold (with a toast)
static int g_grabs[16], g_ngrabs;
static int fakeGrab(int fd, int on) {
    (void)fd;
    if (g_ngrabs < 16) g_grabs[g_ngrabs++] = on;
    return 0;
}

static void auxFrames(XrCtx* c, int n, int focused, int streaming) {
    for (int i = 0; i < n; i++) {
        g_now += FRAME_NS;
        xrAuxButtonFrame(c, g_now, focused, streaming);
    }
}

static void putKey(int fd, int value) {
    struct input_event e;
    memset(&e, 0, sizeof e);
    e.type = EV_KEY;
    e.code = KEY_SELECT;
    e.value = value;
    CHECK(write(fd, &e, sizeof e) == (ssize_t)sizeof e);
}

static void auxGrab(Scene* s) {
    XrCtx* c = s->ctx;
    setUp(s, 0);
    signal(SIGPIPE, SIG_IGN);
    char dir[] = "/tmp/mlxr-auxfifo-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char fifo[600];
    snprintf(fifo, sizeof fifo, "%s/event7", dir);
    CHECK(mkfifo(fifo, 0600) == 0);
    int w = open(fifo, O_RDWR | O_NONBLOCK);   // a writer for as long as the test runs: no EOF
    CHECK(w >= 0);
    c->auxState = ACCESS_BUTTON_OK;
    c->auxInfo.found = c->auxInfo.readable = 1;
    snprintf(c->auxInfo.path, sizeof c->auxInfo.path, "%s", fifo);
    c->auxLookNs = g_now;
    c->auxDev.grab = fakeGrab;
    g_ngrabs = 0;
    // off by default: nothing taken
    xrSetPointerSource(c, POINTER_SOURCE_HEAD, 0);
    auxFrames(c, 3, 1, 1);
    CHECK(c->auxDev.fd < 0 && g_ngrabs == 0);
    // on, but a controller pointing, or not focused, or no stream: still nothing
    float out[IN_SLOTS];
    cogApplyCell(c, COG_TAB_ACCESS, COG_ACCESS_ROW_BUTTON, 1, out);
    CHECK((int)out[IN_SETTING] == SETTING_ASSIST_HEADSET_BUTTON && (int)out[IN_SETTING_VALUE] == 1 && c->assistHeadsetButton);
    auxFrames(c, 3, 0, 1);
    auxFrames(c, 3, 1, 0);
    CHECK(c->auxDev.fd < 0 && g_ngrabs == 0);
    // taken
    auxFrames(c, 2, 1, 1);
    CHECK(c->auxDev.fd >= 0 && c->auxDev.grabbed && g_ngrabs == 1 && g_grabs[0] == 1);
    // one press: a click after the multi-press window
    putKey(w, 1);
    auxFrames(c, 3, 1, 1);
    putKey(w, 0);
    int clicked = 0;
    for (int i = 0; i < 60; i++) {
        auxFrames(c, 1, 1, 1);
        clicked |= c->assistButtons != 0;
    }
    CHECK(clicked);
    // focus lost: given back at once
    auxFrames(c, 1, 0, 1);
    CHECK(c->auxDev.fd < 0 && g_ngrabs == 2 && g_grabs[1] == 0);
    // focus back: taken again; held 3 s: the drag let go, given back for 15 s with a toast
    auxFrames(c, 1, 1, 1);
    CHECK(c->auxDev.fd >= 0 && g_ngrabs == 3);
    putKey(w, 1);
    int dragged = 0;
    for (int i = 0; i < 300; i++) {   // 3.3 s
        auxFrames(c, 1, 1, 1);
        dragged |= c->assistPlay.dragging;
    }
    CHECK(dragged && !c->assistPlay.dragging && c->assistButtons == 0);
    CHECK(c->auxDev.fd < 0 && g_ngrabs == 4 && g_grabs[3] == 0);
    CHECK(c->auxEscapeUntilNs > g_now + 14000000000LL);
    putKey(w, 0);
    auxFrames(c, 900, 1, 1);   // 10 s: still SteamVR's
    CHECK(c->auxDev.fd < 0);
    auxFrames(c, 500, 1, 1);   // past 15 s: taken again
    CHECK(c->auxDev.fd >= 0 && c->auxDev.grabbed);
    // the setting off: given back
    cogApplyCell(c, COG_TAB_ACCESS, COG_ACCESS_ROW_BUTTON, 0, out);
    auxFrames(c, 1, 1, 1);
    CHECK(c->auxDev.fd < 0 && !c->auxDev.grabbed);
    // an unreadable device greys the row; the cell takes no press
    c->auxState = ACCESS_BUTTON_UNREADABLE;
    CHECK(!cogRowLive(c, COG_TAB_ACCESS, COG_ACCESS_ROW_BUTTON));
    out[IN_SETTING] = -1.0f;
    cogApplyCell(c, COG_TAB_ACCESS, COG_ACCESS_ROW_BUTTON, 1, out);
    CHECK(out[IN_SETTING] == -1.0f && !c->assistHeadsetButton);
    c->auxState = ACCESS_BUTTON_UNKNOWN;
    c->auxDev.grab = NULL;
    close(w);
    unlink(fifo);
    rmdir(dir);
    xrSetPointerSource(c, POINTER_SOURCE_CONTROLLER, 0);
}

int main(void) {
    Scene* s = scene_new();
    presses(s);
    neverByItself(s);
    headTurnsItOff(s);
    eyesFallBack(s);
    eyesLine(s);
    footer(s);
    dotScales(s);
    assistClicks(s);
    auxGrab(s);
    scene_free(s);
    return checksDone("m4_access");
}
