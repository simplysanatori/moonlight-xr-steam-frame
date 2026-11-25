// Moonlight XR Native, tests/m4 (1.3.4): every row of every settings tab does what its label says and
// nothing else. Each cell, run and step button the panel draws is pressed at the middle of the hit drawn
// for it, with a controller's ray and trigger through the input pass's seams (as test_replay), in the
// void, in passthrough before and after its questions, in a room that lets its picture be sized, in one
// that does not, with the screen on the head, with the light gun, and on the PC's own pair. The settings
// that come out are then compared whole, key by key, with what the press should have made of them: a
// greyed row makes nothing of it. The tabs' names and the reset buttons go through the same press. GPLv3.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "scene.h"
#include "xr_depth.h"

#define FAKE(n) ((uintptr_t)(n))
#define VIEW ((XrSpace)FAKE(0x703))
#define LOCAL ((XrSpace)FAKE(0x704))
#define RIGHT_AIM ((XrSpace)FAKE(0x702))
#define TRIGGER ((XrAction)FAKE(0x801))
#define FRAME_NS 11111111LL

static const Vec3 HAND = { 0.2f, -0.3f, -0.25f };
static Vec3 g_target = { 0.0f, 0.0f, -2.0f };
static float g_pull;
static int64_t g_now = 1000000000LL;

static XrPosef aimAt(Vec3 from, Vec3 target) {
    Vec3 d = vecNorm(vecSub(target, from));
    Vec3 z = { -d.x, -d.y, -d.z };
    Vec3 up = { 0, 1, 0 };
    Vec3 x = vecNorm(vecCross(up, z));
    Vec3 y = vecCross(z, x);
    XrPosef p = { quatFromBasis(x, y, z), { from.x, from.y, from.z } };
    return p;
}

static XrResult fakeLocate(XrCtx* ctx, XrSpace space, XrSpace base, XrTime time, XrSpaceLocation* out) {
    (void)ctx; (void)base; (void)time;
    out->locationFlags = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT
            | XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
    if (space == VIEW) {
        memset(&out->pose, 0, sizeof(out->pose));
        out->pose.orientation.w = 1.0f;
        return XR_SUCCESS;
    }
    if (space == RIGHT_AIM) {
        out->pose = aimAt(HAND, g_target);
        return XR_SUCCESS;
    }
    return XR_ERROR_HANDLE_INVALID;
}

static int fakePoseActive(XrCtx* ctx, XrAction action, int hand) {
    (void)ctx; (void)action;
    return hand == HAND_RIGHT;
}

static float fakeFloat(XrCtx* ctx, XrAction action, int hand) {
    (void)ctx;
    return action == TRIGGER && hand == HAND_RIGHT ? g_pull : 0.0f;
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

// ---- the states a panel is pressed in ---------------------------------------------------------------------

typedef struct {
    const char* name;
    int cell;            // ENV_CELL_*
    int resizable;
    int headLocked;
    int gun;
    int ptOk;            // passthrough's questions understood
    int source;          // STEREO_SOURCE_*
    int stereo;          // a 3D session
} World;

static const World WORLDS[] = {
    { "void", ENV_CELL_VOID, 0, 0, 0, 0, STEREO_SOURCE_DEPTH, 1 },
    { "void, head locked", ENV_CELL_VOID, 0, 1, 0, 0, STEREO_SOURCE_DEPTH, 1 },
    { "void, the Gun", ENV_CELL_VOID, 0, 1, 1, 0, STEREO_SOURCE_DEPTH, 1 },
    { "void, the PC's pair", ENV_CELL_VOID, 0, 0, 0, 0, STEREO_SOURCE_SBS, 1 },
    { "void, no 3D", ENV_CELL_VOID, 0, 0, 0, 0, STEREO_SOURCE_DEPTH, 0 },
    { "passthrough, first", ENV_CELL_PASSTHROUGH, 0, 0, 0, 0, STEREO_SOURCE_DEPTH, 1 },
    { "passthrough, understood", ENV_CELL_PASSTHROUGH, 0, 0, 0, 1, STEREO_SOURCE_DEPTH, 1 },
    { "theater", ENV_CELL_HOME_THEATER, 1, 0, 0, 0, STEREO_SOURCE_DEPTH, 1 },
    { "theater, head locked", ENV_CELL_HOME_THEATER, 1, 1, 0, 0, STEREO_SOURCE_DEPTH, 1 },
    { "grand cinema", ENV_CELL_GRAND_CINEMA, 0, 0, 0, 0, STEREO_SOURCE_DEPTH, 1 },
    { "synthwave, the Gun", ENV_CELL_SYNTHWAVE, 1, 0, 1, 0, STEREO_SOURCE_DEPTH, 1 },
};

// What a press can change that no file keeps
typedef struct {
    int headAim, padMode, stereoLive, hint, cogOpen, resetPlacement, forgot;
} Toggles;

static Scene* g_scene;
static Settings g_file;      // config.ini as the session would hold it

static int inRoom(const World* w) {
    return w->cell >= ENV_CELL_HOME_THEATER;
}

// The room's index in Settings, -1 for none
static int roomIndex(const World* w) {
    return inRoom(w) ? w->cell - ENV_CELL_HOME_THEATER : -1;
}

// A fresh start in a world, on a tab, from a file
static void start(const World* w, int tab, const Settings* file) {
    Scene* s = g_scene;
    XrCtx* c = s->ctx;
    scene_reset(s);
    g_file = *file;
    s->settings = g_file;
    xrApplySettings(c, &g_file);
    c->passthrough = w->cell == ENV_CELL_PASSTHROUGH;
    c->cylinderSupported = 1;
    c->stereoMode = w->stereo ? DEPTH_MODE_MODEL : DEPTH_MODE_OFF;
    c->depthModeSet = c->stereoMode;
    c->depthModel = MLXR_DEPTH_ZIPDEPTH_256_INT8DR;
    c->depthModelNext = 0;
    c->depthFailed = 0;
    c->stereoSource = w->source;
    c->stereoLive = 1;
    c->padMode = 0;
    c->headAimFlipped = 0;
    c->hintOpen = 0;
    c->poseDirty = 0;
    c->cogDragSlider = -1;
    c->launcher = LAUNCHER_NONE;
    c->notices.level = g_file.notices;
    c->viewSpace = VIEW;
    c->localSpace = LOCAL;
    c->aimSpaces[HAND_LEFT] = XR_NULL_HANDLE;
    c->aimSpaces[HAND_RIGHT] = RIGHT_AIM;
    c->aimAction = (XrAction)FAKE(0x802);
    c->triggerAction = TRIGGER;
    c->profileKind[HAND_LEFT] = PROFILE_NONE;
    c->profileKind[HAND_RIGHT] = PROFILE_CONTROLLER;
    c->locateSpace = fakeLocate;
    c->poseActive = fakePoseActive;
    c->readFloat = fakeFloat;
    c->readBool = fakeBool;
    c->readVec2 = fakeVec2;
    c->pointerAwake = 1;
    c->stillFor = 0.0f;
    c->placementValid = 1;
    g_pull = 0.0f;
    c->cogOpen = 1;
    c->cogTab = tab;
    c->cogPose = cogPanelPose(c, &c->cogW, &c->cogH);
}

// A frame of the pointer and the panel, and what came out of it stored as the session stores it
static int g_forgot;
static void frame(void) {
    XrCtx* c = g_scene->ctx;
    float out[IN_SLOTS];
    g_now += FRAME_NS;
    c->predictedDisplayTime = (XrTime)g_now;
    c->pointerAwake = 1;
    c->stillFor = 0.0f;
    xrPointerPanelStep(c, g_now, g_file.head_locked, out);
    if (c->poseDirty) {
        c->poseDirty = 0;
        float pose[POSE_VALUES];
        xrScreenPoseOut(c, pose);
        xrPoseToSettings(pose, xrCurveSetting(c), &g_file);
    }
    if (out[IN_SETTING] >= 0.0f) {
        g_forgot += (int)out[IN_SETTING] == SETTING_FORGET_LAYOUT;
        xrStoreSetting(c, &g_file, (int)out[IN_SETTING], (int)out[IN_SETTING_VALUE]);
    }
}

// The ray at a point of the panel, and the trigger pulled and let go there
static void aimPanel(float u, float v) {
    XrCtx* c = g_scene->ctx;
    Vec3 local = { (u - 0.5f) * c->cogW, (0.5f - v) * c->cogH, 0.0f };
    Vec3 off = quatRotate(c->cogPose.orientation, local);
    g_target = (Vec3){ c->cogPose.position.x + off.x, c->cogPose.position.y + off.y, c->cogPose.position.z + off.z };
    g_forgot = 0;
    for (int i = 0; i < 40; i++) frame();
}

static void pull(void) {
    g_pull = 1.0f;
    for (int i = 0; i < 3; i++) frame();
    g_pull = 0.0f;
    for (int i = 0; i < 3; i++) frame();
}

static void press(float u, float v) {
    aimPanel(u, v);
    pull();
}

// What the ray lights up on the panel: a live row's cell (or step button, or its run), and nothing on a
// greyed one
static void hovers(const World* w, int tab, int row, int live, int cell, int step) {
    XrCtx* c = g_scene->ctx;
    int ok = live ? c->cogHoverSlider == row && c->cogHoverCell == cell && c->cogHoverStep == step
                  : c->cogHoverSlider == -1 && c->cogHoverCell == -1 && c->cogHoverStep == 0;
    CHECK(ok);
    if (!ok) {
        fprintf(stderr, "  %s, tab %d row %d cell %d step %d live %d: the hover is on row %d cell %d step %d\n", w->name, tab, row,
                cell, step, live, c->cogHoverSlider, c->cogHoverCell, c->cogHoverStep);
    }
}

static Toggles toggles(void) {
    XrCtx* c = g_scene->ctx;
    Toggles t = { headAimSwitchOn(c->headAimSetting, c->headAimFlipped), c->padMode, c->stereoLive,
                  c->hintOpen ? c->hintKind + 1 : 0, c->cogOpen, !c->placementValid, g_forgot };
    return t;
}

// The panel as it is drawn now, and where it can be pressed
static UiHits g_hits;
static void draw(void) {
    Scene* s = g_scene;
    scene_frame(s, NULL);
    const UiView* v = xrPanelsLastView(s->ctx);
    static uint8_t px[COG_TEX_W * COG_TEX_H * 4];
    UiCanvas cv;
    ui_cv_init(&cv, px, COG_TEX_W, COG_TEX_H);
    ui_cv_set_clip(&cv, 0.0f, 0.0f, 0.0f, 0.0f);   // only where it can be pressed is read, not the pixels
    ui_hits_clear(&g_hits);
    ui_draw_panel(UI_PANEL_COG, &cv, v, &g_hits);
}

static int wrong;
static void same(const World* w, int tab, int row, const char* what, const Settings* want, const Toggles* wt) {
    int n = 0;
    const SettingDesc* d = settings_table(&n);
    for (int i = 0; i < n; i++) {
        int ok = d[i].type == SET_STR ? strcmp(settings_get_str(want, &d[i]), settings_get_str(&g_file, &d[i])) == 0
                                      : settings_get(want, &d[i]) == settings_get(&g_file, &d[i]);
        CHECK(ok);
        if (!ok && wrong++ < 40) {
            fprintf(stderr, "  %s, tab %d row %d %s: %s is %d, wanted %d\n", w->name, tab, row, what, d[i].key,
                    d[i].type == SET_STR ? -1 : settings_get(&g_file, &d[i]), d[i].type == SET_STR ? -1 : settings_get(want, &d[i]));
        }
    }
    Toggles got = toggles();
    int ok = memcmp(&got, wt, sizeof(got)) == 0;
    CHECK(ok);
    if (!ok && wrong++ < 40) {
        fprintf(stderr, "  %s, tab %d row %d %s: aim %d pad %d 3D %d hint %d open %d reset %d forgot %d, wanted %d %d %d %d %d %d %d\n",
                w->name, tab, row, what, got.headAim, got.padMode, got.stereoLive, got.hint, got.cogOpen, got.resetPlacement,
                got.forgot, wt->headAim, wt->padMode, wt->stereoLive, wt->hint, wt->cogOpen, wt->resetPlacement, wt->forgot);
    }
}

// ---- what each row is for ----------------------------------------------------------------------------------

static int alive(const World* w, int tab, int row) {
    int room = inRoom(w);
    switch (tab) {
    case COG_TAB_SCREEN: return !room || row == 8 || (row == 5 && w->resizable);
    case COG_TAB_ROOM: return room || row < 4;
    case COG_TAB_3D: return row == 1 || (w->stereo && (w->source == STEREO_SOURCE_DEPTH || row == 0 || row == 5));
    case COG_TAB_CONTROLS:
        return row >= 8 ? w->headLocked && !room : !(w->gun && (row == 4 || row == 5));
    case COG_TAB_ACCESS: return row == 0;   // the controllers point, so the rest have nothing to set
    default: return 1;
    }
}

// What the press of a cell makes of the settings
static void cellMakes(const World* w, int tab, int row, int cell, Settings* s, Toggles* t) {
    int r = roomIndex(w);
    switch (tab * 100 + row) {
    case COG_TAB_SCREEN * 100 + 6:
        if (cell < 3) {
            s->screen_distance_cm = layoutPresetOf(cell).distanceCm;
            s->screen_width_cm = layoutPresetOf(cell).widthCm;
        }
        break;   // Bring here: dead ahead at the start already
    case COG_TAB_SCREEN * 100 + 7: s->head_locked = cell == 1; s->head_follow = cell == 2; break;
    case COG_TAB_SCREEN * 100 + 8:
        if (cell == 2) t->forgot = 1;
        else s->per_app = cell;
        break;
    case COG_TAB_SCREEN * 100 + 9: s->angular_lock = cell; break;
    case COG_TAB_ROOM * 100 + 1:
        if (r >= 0) s->room[r].glow = cell;
        else if (cell && w->cell == ENV_CELL_PASSTHROUGH && !w->ptOk) { t->hint = HINT_KIND_PT_GLOW + 1; t->cogOpen = 0; }
        else s->glow = cell;
        break;
    case COG_TAB_ROOM * 100 + 5: s->screen_light = cell; break;
    case COG_TAB_PICTURE * 100 + 4: s->sharpening = cell; break;
    case COG_TAB_PICTURE * 100 + 5: s->supersampling = cell; break;
    case COG_TAB_PICTURE * 100 + 6: s->foveated = cell ? FOVEATED_ON : FOVEATED_OFF; break;
    case COG_TAB_PICTURE * 100 + 7:
        s->stats = cell > 0;
        if (cell > 0) s->stats_style = cell - 1;
        break;
    case COG_TAB_PICTURE * 100 + 8: s->notices = cell; break;
    case COG_TAB_3D * 100 + 0: t->stereoLive = cell; break;
    case COG_TAB_3D * 100 + 1: s->stereo_source = cell; break;
    case COG_TAB_3D * 100 + 2:
        if (cell == COG_MODEL_FULL) { t->hint = HINT_KIND_DEPTH_FULL + 1; t->cogOpen = 0; }
        break;   // Light is the one in force
    case COG_TAB_3D * 100 + 3: {
        int presets[COG_PRESET_CELLS];
        ui_presets_for(s, presets);
        s->separation = presets[cell];
        break;
    }
    case COG_TAB_CONTROLS * 100 + 0: t->padMode = cell; break;
    case COG_TAB_CONTROLS * 100 + 1: s->mouse_mode = cell; break;
    case COG_TAB_CONTROLS * 100 + 2:
        if (w->cell == ENV_CELL_PASSTHROUGH && !w->ptOk) { t->hint = HINT_KIND_PT_CONTROLLERS + 1; t->cogOpen = 0; }
        else s->controller_model = cell;
        break;
    case COG_TAB_CONTROLS * 100 + 3: s->show_ray = cell == 2; s->pointer_dot = cell >= 1; break;
    case COG_TAB_CONTROLS * 100 + 4: s->pointer_sleep = cell; break;
    case COG_TAB_CONTROLS * 100 + 5: s->click_steadiness = cell; break;
    case COG_TAB_CONTROLS * 100 + 6: s->click_sound = cell; break;
    case COG_TAB_CONTROLS * 100 + 8: t->headAim = cell; break;
    case COG_TAB_ACCESS * 100 + 0: s->pointer_source = cell; break;
    default: CHECK(0); break;
    }
}

// A track: the key it sets, and what a press along its run (at the middle, or at AT) and a step down and
// up from its start value make of it
typedef struct {
    int tab, row;
    const char* key;     // the room's own: with the room's number for %d
    int from, middle, down, up;
    float at;            // where the run is pressed, 0 for the middle: off a place between two steps
} Track;
#define AT 0.4f

static const Track TRACKS[] = {
    { COG_TAB_SCREEN, 0, "distance_cm", 300, 410, 290, 310 },
    { COG_TAB_SCREEN, 1, "height_cm", 50, 0, 45, 55 },
    { COG_TAB_SCREEN, 2, "tilt_deg", 10, 0, 7, 12 },
    { COG_TAB_SCREEN, 3, "rotate_deg", 12, 0, 6, 18 },
    { COG_TAB_SCREEN, 4, "curve", 20, 50, 15, 25 },
    { COG_TAB_SCREEN, 5, "width_cm", 300, 368, 290, 310, AT },
    { COG_TAB_ROOM, 0, "background_dim", 20, 50, 15, 25 },
    { COG_TAB_ROOM, 2, "glow_level", 30, 50, 25, 35 },
    { COG_TAB_ROOM, 3, "glow_spread", 30, 55, 25, 35 },
    { COG_TAB_ROOM, 4, "room_%d_brightness", 60, 0, 55, 65 },
    { COG_TAB_ROOM, 6, "room_%d_light", 60, 0, 55, 65 },
    { COG_TAB_PICTURE, 0, "picture_brightness", 10, 0, 9, 11 },
    { COG_TAB_PICTURE, 1, "picture_contrast", 120, 100, 119, 121 },
    { COG_TAB_PICTURE, 2, "picture_gamma", 90, 125, 89, 91 },
    { COG_TAB_PICTURE, 3, "picture_saturation", 120, 100, 119, 121 },
    { COG_TAB_3D, 4, "separation", 4, 6, 3, 5, AT },
    { COG_TAB_3D, 5, "convergence", 30, 50, 29, 31 },
    { COG_TAB_3D, 6, "edge_sharpness", 25, 50, 0, 50 },
    { COG_TAB_3D, 7, "depth_scale", 120, 100, 110, 130 },
    { COG_TAB_3D, 8, "depth_global", 120, 100, 110, 130 },
    { COG_TAB_3D, 9, "motion_reset", 10, 25, 5, 15 },
    { COG_TAB_3D, 10, "eye_balance", 20, 0, 10, 30 },
    { COG_TAB_CONTROLS, 7, "scroll_speed", 75, 100, 50, 100, AT },
    { COG_TAB_CONTROLS, 9, "head_aim_sensitivity", 10, 13, 9, 11, AT },
    { COG_TAB_CONTROLS, 10, "head_aim_deadzone", 5, 10, 4, 6 },
};

// The key and the value of a track in a world: Size in a room is the room's own, in whole percent
static void trackIn(const World* w, const Track* t, char* key, size_t cap, int* from, int* middle, int* down, int* up) {
    *from = t->from;
    *middle = t->middle;
    *down = t->down;
    *up = t->up;
    int env = 3 + roomIndex(w) + 1;   // ENV_ID_HOME_THEATER 4 and on
    if (inRoom(w) && t->tab == COG_TAB_SCREEN && t->row == 5) {
        snprintf(key, cap, "room_%d_screen", env);
        *from = 60;
        *middle = laneUnits(AT, ROOM_SCREEN_MIN, ROOM_SCREEN_MAX);
        *down = 59;
        *up = 61;
        return;
    }
    snprintf(key, cap, t->key, env);
    if (t->tab == COG_TAB_ROOM && t->row == 4) *middle = laneUnits(0.5f, ROOM_BRIGHTNESS_MIN, ROOM_BRIGHTNESS_MAX);
    if (t->tab == COG_TAB_ROOM && t->row == 6) *middle = laneUnits(0.5f, ROOM_LIGHT_MIN, ROOM_LIGHT_MAX);
}

// The file a world starts from: the placement dead ahead, nothing at its default that a row could
// leave alone by luck
static Settings startFile(const World* w) {
    Settings f;
    settings_defaults(&f);
    f.environment = xrEnvironmentSetting(w->cell);
    f.passthrough = w->cell == ENV_CELL_PASSTHROUGH;
    f.passthrough_controllers_ok = f.passthrough_glow_ok = w->ptOk;
    f.head_locked = w->headLocked;
    f.screen_distance_cm = 300;
    f.screen_width_cm = 300;
    f.screen_height_cm = 0;
    f.lightgun = w->gun ? LIGHTGUN_ON : LIGHTGUN_OFF;
    f.depth_source = DEPTH_SOURCE_LIGHT;
    f.stereo_source = w->source;
    f.foveated = FOVEATED_OFF;
    f.separation = 6;
    f.convergence = 40;
    settings_clamp(&f);
    return f;
}

// The file as the session has it once started and stopped with nothing pressed
static Settings settled(const World* w, int tab, const Settings* file) {
    start(w, tab, file);
    XrCtx* c = g_scene->ctx;
    float pose[POSE_VALUES];
    xrScreenPoseOut(c, pose);
    Settings f = *file;
    xrPoseToSettings(pose, xrCurveSetting(c), &f);
    return f;
}

static void pressCell(const World* w, int tab, int row, int cell) {
    int option = cogRowOption(tab, row);
    int cells = cogRowCells(tab, row);
    // from the cell before it, so a press that did nothing shows
    Settings f = startFile(w);
    Toggles none;
    memset(&none, 0, sizeof(none));
    if (option >= 0 || tab == COG_TAB_SCREEN || tab == COG_TAB_3D) {
        Toggles t = none;
        int before = (cell + cells - 1) % cells;
        if (!(tab == COG_TAB_SCREEN && row == COG_SCREEN_ROW_PER_APP && before == 2)
                && !(tab == COG_TAB_3D && (row == COG_ROW3D_SOURCE || row == COG_ROW3D_MODEL))
                && option != COG_OPTION_HEAD_LOCK && option != COG_OPTION_CONTROLLERS) {
            World plain = *w;
            plain.ptOk = 1;
            cellMakes(&plain, tab, row, before, &f, &t);
        }
        settings_clamp(&f);
    }
    f = settled(w, tab, &f);
    start(w, tab, &f);
    XrCtx* c = g_scene->ctx;
    // the session's own switches, from the other state
    if (option == COG_OPTION_HEAD_AIM) setHeadAimOn(c, !cell, "the test");
    if (tab == COG_TAB_3D && row == COG_ROW3D_SWITCH) c->stereoLive = !cell;
    frame();
    draw();
    int i = ui_hits_find(&g_hits, UI_HIT_CELL, row, cell);
    int live = alive(w, tab, row);
    if (tab == COG_TAB_ACCESS && row == COG_ACCESS_ROW_POINTER && cell == POINTER_SOURCE_GAZE) live = 0;   // no eyes offered
    if (i < 0) {
        // Only the depth model's place, which says why there is no 3D
        CHECK(tab == COG_TAB_3D && row == COG_ROW3D_MODEL && !w->stereo);
        return;
    }
    const UiHit hit = g_hits.hit[i];
    CHECK(hit.enabled == live);
    Settings want = g_file;
    g_forgot = 0;
    Toggles wt = toggles();
    if (live) {
        cellMakes(w, tab, row, cell, &want, &wt);
        settings_clamp(&want);
    }
    aimPanel((hit.u0 + hit.u1) * 0.5f, (hit.v0 + hit.v1) * 0.5f);
    // (the pointer row's Eyes, dimmed alone, still lights up: the footer says why)
    hovers(w, tab, row, live || (tab == COG_TAB_ACCESS && row == COG_ACCESS_ROW_POINTER), cell, 0);
    pull();
    char what[32];
    snprintf(what, sizeof what, "cell %d", cell);
    same(w, tab, row, what, &want, &wt);
    // and the ring the panel draws is on it: an option's, where the panel is still up
    if (live && option >= 0 && c->cogOpen && option != COG_OPTION_GAMEPAD) {
        g_scene->settings = g_file;
        draw();
        CHECK(ui_cog_option_cell(xrPanelsLastView(c), option) == cell);
    }
}

// Height moves the screen up and down where it hangs, so its distance from the seat comes out a little
// more or less: the file has the pose's own
static void withHeight(const World* w, const Track* t, Settings* want) {
    if (!inRoom(w) && t->tab == COG_TAB_SCREEN && t->row == COG_SLIDER_HEIGHT) {
        XrVector3f p = g_scene->ctx->screenPose.position;
        want->screen_distance_cm = (int)lroundf(sqrtf(p.x * p.x + p.y * p.y + p.z * p.z) * 100.0f);
    }
}

static void pressTrack(const World* w, const Track* t) {
    char key[40];
    int from, middle, down, up;
    trackIn(w, t, key, sizeof key, &from, &middle, &down, &up);
    static const int KINDS[3] = { UI_HIT_TRACK_RUN, UI_HIT_TRACK_DOWN, UI_HIT_TRACK_UP };
    static const char* const NAMES[3] = { "run", "down", "up" };
    int live = alive(w, t->tab, t->row);
    for (int k = 0; k < 3; k++) {
        Settings f = startFile(w);
        const SettingDesc* d = settings_find(key);
        if (d == NULL) {   // a room's row outside a room: its first room's key, which must stay
            CHECK(!live);
        } else {
            settings_set(&f, d, from);
        }
        settings_clamp(&f);
        f = settled(w, t->tab, &f);
        start(w, t->tab, &f);
        frame();
        draw();
        int i = ui_hits_find(&g_hits, KINDS[k], t->row, -1);
        CHECK(i >= 0);
        if (i < 0) continue;
        const UiHit hit = g_hits.hit[i];
        CHECK(hit.enabled == live);
        if (hit.enabled != live) fprintf(stderr, "  %s, tab %d row %d: enabled %d, live %d\n", w->name, t->tab, t->row, hit.enabled, live);
        Settings want = g_file;
        g_forgot = 0;
        Toggles wt = toggles();
        if (live && d != NULL) {
            settings_set(&want, d, k == 0 ? middle : k == 1 ? down : up);
            settings_clamp(&want);
        }
        aimPanel(k == 0 && t->at > 0.0f ? cogRunU(t->at) : (hit.u0 + hit.u1) * 0.5f, (hit.v0 + hit.v1) * 0.5f);
        hovers(w, t->tab, t->row, live, -1, k == 0 ? 0 : k == 1 ? -1 : 1);
        pull();
        withHeight(w, t, &want);
        same(w, t->tab, t->row, NAMES[k], &want, &wt);
        if (live && k == 0) {
            // the thumb drawn after it sits where the press was
            CHECK_NEAR(cogSliderValue(g_scene->ctx, t->tab, t->row),
                       t->at > 0.0f ? t->at : 0.5f, 0.04);
        }
    }
}

// A drag from the middle of the run to its end and off the panel sets that end
static void dragTrack(const World* w, const Track* t) {
    char key[40];
    int from, middle, down, up;
    trackIn(w, t, key, sizeof key, &from, &middle, &down, &up);
    if (!alive(w, t->tab, t->row)) return;
    const SettingDesc* d = settings_find(key);
    Settings f = startFile(w);
    settings_set(&f, d, from);
    settings_clamp(&f);
    f = settled(w, t->tab, &f);
    start(w, t->tab, &f);
    XrCtx* c = g_scene->ctx;
    Settings want = g_file;
    g_forgot = 0;
    Toggles wt = toggles();
    float v = cogRowV(t->tab, t->row);
    for (int i = 0; i < 60; i++) {
        float u = i < 30 ? 0.5f * (COG_RUN_L + COG_RUN_R) : COG_RUN_R + 0.01f;
        Vec3 local = { (u - 0.5f) * c->cogW, (0.5f - v) * c->cogH, 0.0f };
        Vec3 off = quatRotate(c->cogPose.orientation, local);
        g_target = (Vec3){ c->cogPose.position.x + off.x, c->cogPose.position.y + off.y, c->cogPose.position.z + off.z };
        g_pull = i >= 25 ? 1.0f : 0.0f;
        // nothing is stored until it is let go
        Settings before = g_file;
        frame();
        CHECK(settings_equal(&before, &g_file));
    }
    g_pull = 0.0f;
    for (int i = 0; i < 3; i++) frame();
    int end = settings_get(&g_file, d);
    settings_set(&want, d, end);
    withHeight(w, t, &want);
    same(w, t->tab, t->row, "drag", &want, &wt);
    CHECK(fabsf(cogSliderValue(c, t->tab, t->row) - 1.0f) < 0.02f);
    CHECK(end != settings_get(&f, d));
}

static void everyRow(void) {
    for (size_t n = 0; n < sizeof WORLDS / sizeof WORLDS[0]; n++) {
        const World* w = &WORLDS[n];
        int tracks = 0, cellsPressed = 0;
        for (int tab = 0; tab < COG_TAB_COUNT; tab++) {
            for (int row = 0; row < cogTabRowCount(tab); row++) {
                if (cogRowIsTrack(tab, row)) {
                    int known = 0;
                    for (size_t i = 0; i < sizeof TRACKS / sizeof TRACKS[0]; i++) {
                        if (TRACKS[i].tab == tab && TRACKS[i].row == row) {
                            pressTrack(w, &TRACKS[i]);
                            dragTrack(w, &TRACKS[i]);
                            known = 1;
                        }
                    }
                    // the Accessibility tab's dot size is dead with the controllers pointing
                    CHECK(known || tab == COG_TAB_ACCESS);
                    tracks += known;
                    continue;
                }
                for (int cell = 0; cell < cogRowCells(tab, row); cell++) {
                    if (tab == COG_TAB_ACCESS && row == COG_ACCESS_ROW_POINTER && cell == POINTER_SOURCE_HEAD) {
                        continue;   // takes the pointer off the hand: test_access
                    }
                    pressCell(w, tab, row, cell);
                    cellsPressed++;
                }
            }
        }
        CHECK(tracks == (int)(sizeof TRACKS / sizeof TRACKS[0]));
        CHECK(cellsPressed > 60);
    }
}

// ---- the resets ---------------------------------------------------------------------------------------------

static void resets(void) {
    for (size_t n = 0; n < sizeof WORLDS / sizeof WORLDS[0]; n++) {
        const World* w = &WORLDS[n];
        for (int tab = 0; tab < COG_TAB_COUNT; tab++) {
            Settings f = startFile(w);
            f.picture_brightness = 20;
            f.picture_contrast = 120;
            f.picture_gamma = 130;
            f.picture_saturation = 140;
            f.bg_dim = 30;
            f.glow_level = 80;
            f.glow_spread = 90;
            f.separation = 11;
            f.convergence = 70;
            for (int r = 0; r < 3; r++) {
                f.room[r].screen = r == 1 ? 100 : 40;
                f.room[r].brightness = 80;
                f.room[r].light = 40;
            }
            settings_clamp(&f);
            f = settled(w, tab, &f);
            start(w, tab, &f);
            frame();
            draw();
            int has = tab <= COG_TAB_3D;
            int live = has && (tab != COG_TAB_SCREEN || !inRoom(w) || w->resizable);
            int i = ui_hits_find(&g_hits, UI_HIT_BUTTON, UI_BTN_RESET, -1);
            CHECK((i >= 0) == has);
            if (i >= 0) CHECK(g_hits.hit[i].enabled == live);
            if (i >= 0 && g_hits.hit[i].enabled != live) fprintf(stderr, "  %s, tab %d reset: enabled %d\n", w->name, tab, !live);
            Settings want = g_file;
            g_forgot = 0;
            Toggles wt = toggles();
            if (live) {
                if (tab == COG_TAB_SCREEN && inRoom(w)) want.room[roomIndex(w)].screen = w->cell == ENV_CELL_SYNTHWAVE ? ROOM_SYNTHWAVE_SCREEN : ROOM_THEATER_SCREEN;
                else if (tab == COG_TAB_SCREEN) wt.resetPlacement = 1;
                else if (tab == COG_TAB_ROOM) {
                    want.bg_dim = BG_DIM_DEFAULT;
                    want.glow_level = GLOW_LEVEL_DEFAULT;
                    want.glow_spread = GLOW_SPREAD_DEFAULT;
                } else if (tab == COG_TAB_PICTURE) {
                    settings_reset_picture(&want);
                    CHECK(want.bg_dim == 30 && want.glow_spread == 90);
                } else {
                    settings_reset_3d(&want);
                }
                settings_clamp(&want);
            }
            press((COG_RESET_L + COG_RESET_R) * 0.5f, (COG_RESET_T + COG_RESET_B) * 0.5f);
            if (tab == COG_TAB_CONTROLS || tab == COG_TAB_ACCESS || tab == COG_TAB_ABOUT) {
                // no button there: the last rows are, or nothing
                continue;
            }
            same(w, tab, -1, "reset", &want, &wt);
            if (tab == COG_TAB_ROOM) {
                XrCtx* c = g_scene->ctx;
                CHECK(c->bgDim == BG_DIM_DEFAULT && c->glowSpread == GLOW_SPREAD_DEFAULT);
                CHECK_NEAR(c->ambiIntensity, GLOW_LEVEL_DEFAULT / 100.0, 1e-6);
            }
        }
    }
}

// ---- the tabs -----------------------------------------------------------------------------------------------

// A press on a name opens that tab; the file gets the nearest name an older release has, and the panel
// stays on the tab itself when the settings are applied again
static void tabs(void) {
    static const int FILE_TAB[COG_TAB_COUNT] = { 0, 0, 3, 2, 1, 4, 5 };
    for (int from = 0; from < COG_TAB_COUNT; from++) {
        for (int to = 0; to < COG_TAB_COUNT; to++) {
            Settings f = startFile(&WORLDS[0]);
            start(&WORLDS[0], from, &f);
            frame();
            draw();
            int i = ui_hits_find(&g_hits, UI_HIT_TAB, to, -1);
            CHECK(i >= 0);
            if (i < 0) continue;
            Settings want = g_file;
            press((g_hits.hit[i].u0 + g_hits.hit[i].u1) * 0.5f, (g_hits.hit[i].v0 + g_hits.hit[i].v1) * 0.5f);
            CHECK(g_scene->ctx->cogTab == to);
            want.cog_tab = FILE_TAB[to];
            CHECK(settings_equal(&want, &g_file));
            xrApplySettings(g_scene->ctx, &g_file);
            CHECK(g_scene->ctx->cogTab == to);
        }
    }
    // a file's own choice opens its tab, display's on Picture
    static const struct { const char* name; int tab; } FILES[] = {
        { "screen", COG_TAB_SCREEN }, { "display", COG_TAB_PICTURE }, { "3d", COG_TAB_3D }, { "picture", COG_TAB_PICTURE },
        { "accessibility", COG_TAB_ACCESS }, { "about", COG_TAB_ABOUT },
    };
    for (size_t k = 0; k < sizeof FILES / sizeof FILES[0]; k++) {
        char text[64];
        snprintf(text, sizeof text, "[display]\ncog_tab = %s\n", FILES[k].name);
        Settings f;
        settings_defaults(&f);
        settings_from_text(&f, text, NULL);
        XrCtx* fresh = calloc(1, sizeof(XrCtx));
        xrCtxDefaults(fresh, 2560, 1440, 90);
        xrApplySettings(fresh, &f);
        CHECK(fresh->cogTab == FILES[k].tab);
        free(fresh);
    }
}

// ---- a room that comes or goes under a drag -----------------------------------------------------------------

static void roomUnderADrag(void) {
    for (int into = 0; into < 2; into++) {
        const World* w = into ? &WORLDS[0] : &WORLDS[7];
        Settings f = startFile(w);
        f.room[0].screen = 60;
        f = settled(w, COG_TAB_SCREEN, &f);
        start(w, COG_TAB_SCREEN, &f);
        XrCtx* c = g_scene->ctx;
        float v = cogRowV(COG_TAB_SCREEN, COG_SLIDER_SIZE), u = cogRunU(0.5f);
        Vec3 local = { (u - 0.5f) * c->cogW, (0.5f - v) * c->cogH, 0.0f };
        Vec3 off = quatRotate(c->cogPose.orientation, local);
        g_target = (Vec3){ c->cogPose.position.x + off.x, c->cogPose.position.y + off.y, c->cogPose.position.z + off.z };
        for (int i = 0; i < 30; i++) frame();
        g_pull = 1.0f;
        for (int i = 0; i < 3; i++) frame();
        CHECK(c->cogDragSlider == COG_SLIDER_SIZE);
        Settings before = g_file;
        int screen = c->roomScreen[ROOM_STYLE_THEATER];
        xrSetEnvironment(c, into ? ENV_CELL_HOME_THEATER : ENV_CELL_VOID);
        for (int i = 0; i < 3; i++) frame();
        CHECK(c->cogDragSlider == -1);
        // out of a room there is nothing left to write the drag to; into one the placement it moved is kept
        CHECK(g_file.room[0].screen == before.room[0].screen && c->roomScreen[ROOM_STYLE_THEATER] == screen);
        if (!into) CHECK(g_file.screen_width_cm == before.screen_width_cm);
        g_pull = 0.0f;
        for (int i = 0; i < 3; i++) frame();
    }
}

// ---- the wheel speed past its track -------------------------------------------------------------------------

static void scrollPastTheEnd(void) {
    Settings f = startFile(&WORLDS[0]);
    f.scroll_speed = 400;
    start(&WORLDS[0], COG_TAB_CONTROLS, &f);
    frame();
    draw();
    int i = ui_hits_find(&g_hits, UI_HIT_TRACK_UP, COG_CONTROLS_ROW_SCROLL, -1);
    press((g_hits.hit[i].u0 + g_hits.hit[i].u1) * 0.5f, (g_hits.hit[i].v0 + g_hits.hit[i].v1) * 0.5f);
    CHECK(g_file.scroll_speed == 400 && g_scene->ctx->scrollSpeed == 400);
    i = ui_hits_find(&g_hits, UI_HIT_TRACK_DOWN, COG_CONTROLS_ROW_SCROLL, -1);
    press((g_hits.hit[i].u0 + g_hits.hit[i].u1) * 0.5f, (g_hits.hit[i].v0 + g_hits.hit[i].v1) * 0.5f);
    CHECK(g_file.scroll_speed == 300 && g_scene->ctx->scrollSpeed == 300);
}

// The stats row: Off keeps the style in the file, so a later on (the file's, or an older release's row) is
// the same window
static void statsKeepTheirStyle(void) {
    Settings f = startFile(&WORLDS[0]);
    f.stats = 1;
    f.stats_style = 1;
    start(&WORLDS[0], COG_TAB_PICTURE, &f);
    frame();
    draw();
    int i = ui_hits_find(&g_hits, UI_HIT_CELL, COG_PICTURE_ROW_STATS, 0);
    press((g_hits.hit[i].u0 + g_hits.hit[i].u1) * 0.5f, (g_hits.hit[i].v0 + g_hits.hit[i].v1) * 0.5f);
    CHECK(g_file.stats == 0 && g_file.stats_style == 1);
    CHECK(!g_scene->ctx->overlayVisible && g_scene->ctx->statsStyle == 1);
}

int main(void) {
    g_scene = scene_new();
    everyRow();
    resets();
    tabs();
    roomUnderADrag();
    scrollPastTheEnd();
    statsKeepTheirStyle();
    if (wrong > 40) fprintf(stderr, "  and %d more\n", wrong - 40);
    scene_free(g_scene);
    return checksDone("m4 cogrows");
}
