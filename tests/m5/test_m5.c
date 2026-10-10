// Moonlight XR Native, tests/m5: the rooms, the glow, the models and the click
// through the ported functions, without a GPU or a headset. Groups (one ctest
// entry each):
//   room    where each room hangs the picture (applyRoomPlacement against
//           rooms/room_logic's roomPlacement and upstream's own numbers),
//           the hold and restore of the user's placement, the levels and
//           their clamps, the Room tab's rows, config.ini
//   env     the environment state machine: void, passthrough and each room,
//           in every order, with the blend mode, the layers and the
//           placement checked at each step
//   budget  the layers of the busiest frames with the room, the glow, the
//           models, the UI and the picture (flat and curved), the drop order,
//           and the swapchains the session makes at most
//   click   the click hook (IN_CLICK, the setting, the Display tab's row)
// GPLv3.
#include <stdlib.h>

#include "check.h"
#include "scene.h"
#include "xr_world.h"

// Stand ins for the swapchains the session would have made
#define FAKE_ROOM ((XrSwapchain)(uintptr_t)0x7001)
#define FAKE_MODEL ((XrSwapchain)(uintptr_t)0x7002)
#define FAKE_GLOW ((XrSwapchain)(uintptr_t)0x7003)

static const float ASPECTS[] = { 9.0f / 16.0f, 3.0f / 4.0f, 9.0f / 21.0f, 16.0f / 9.0f };

static int hasSwapchain(const XrFrameOut* out, XrSwapchain sc) {
    for (uint32_t i = 0; i < out->count; i++) {
        const XrCompositionLayerBaseHeader* b = out->order[i];
        if (b->type == XR_TYPE_COMPOSITION_LAYER_QUAD
                && ((const XrCompositionLayerQuad*)b)->subImage.swapchain == sc) return 1;
        if (b->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION && ((const XrCompositionLayerProjection*)b)->views
                && ((const XrCompositionLayerProjection*)b)->views[0].subImage.swapchain == sc) return 1;
    }
    return 0;
}

// What the session's GPU side would have up: the room drawn, the glow drawn
// and copied, the models drawn
static void fakeWorld(XrCtx* c) {
    c->roomSwapchain = FAKE_ROOM;
    c->roomRendered = 1;
    c->roomViewsValid = 1;
    c->roomEyeWidth = c->roomEyeHeight = 1728;
    for (int e = 0; e < 2; e++) {
        c->roomViews[e].pose.orientation.w = 1.0f;
        c->modelViews[e].pose.orientation.w = 1.0f;
    }
    c->glowSwapchain = FAKE_GLOW;
    c->glowSwapchainFilled = 1;
    c->glowRendered = 1;
    c->modelSwapchain = FAKE_MODEL;
    c->modelsShowing = 1;
    c->modelRendered = 1;
    c->modelEyeWidth = c->modelEyeHeight = 1728;
}

// ---- room --------------------------------------------------------------------------

static void roomGroup(void) {
    Scene* s = scene_new();
    XrCtx* c = s->ctx;

    // The table's own values (upstream roomLevelsFromTable; xr_shared.h)
    roomLevelsFromTable(c);
    int bright[4] = { 0, ROOM_THEATER_BRIGHTNESS, ROOM_GRAND_CINEMA_BRIGHTNESS, ROOM_SYNTHWAVE_BRIGHTNESS };
    int glow[4] = { 0, ROOM_THEATER_GLOW, ROOM_GRAND_CINEMA_GLOW, ROOM_SYNTHWAVE_GLOW };
    int screen[4] = { 0, ROOM_THEATER_SCREEN, ROOM_GRAND_CINEMA_SCREEN, ROOM_SYNTHWAVE_SCREEN };
    for (int st = ROOM_STYLE_FIRST; st <= ROOM_STYLE_LAST; st++) {
        CHECK(c->roomBrightness[st] == bright[st]);
        CHECK(c->roomGlow[st] == glow[st]);
        CHECK(c->roomLightLevel[st] == ROOM_LIGHT_DEFAULT);
        CHECK(c->roomScreen[st] == screen[st]);
        // ... and they are what config.ini starts from
        const RoomSettings* r = &s->settings.room[st - ROOM_STYLE_FIRST];
        CHECK(r->brightness == bright[st] && r->glow == glow[st] && r->light == ROOM_LIGHT_DEFAULT
              && r->screen == screen[st]);
    }
    CHECK(roomResizable(ROOM_STYLE_THEATER) && !roomResizable(ROOM_STYLE_GRAND_CINEMA)
          && roomResizable(ROOM_STYLE_SYNTHWAVE) && !roomResizable(0));

    // nativeSetRoomLevels' clamps, by picker cell
    xrSetRoomLevels(c, ENV_CELL_HOME_THEATER, 999, 7, -5, 10);
    CHECK(c->roomBrightness[1] == ROOM_BRIGHTNESS_MAX && c->roomGlow[1] == 1 && c->roomLightLevel[1] == 0
          && c->roomScreen[1] == ROOM_SCREEN_MIN);
    xrSetRoomLevels(c, ENV_CELL_GRAND_CINEMA, 1, 0, 500, 50);
    CHECK(c->roomBrightness[2] == ROOM_BRIGHTNESS_MIN && c->roomLightLevel[2] == ROOM_LIGHT_MAX);
    CHECK(c->roomScreen[2] == ROOM_SCREEN_MAX); // a fixed room keeps the whole anchor
    xrSetRoomLevels(c, ENV_CELL_VOID, 50, 1, 50, 50); // not a room: ignored
    CHECK(roomScreenPercent(c, 0) == ROOM_SCREEN_MAX);
    roomLevelsFromTable(c);

    // The glow's switch: a room's own in a room, the app wide one elsewhere
    c->ambilightOn = 0;
    CHECK(roomGlowOn(c, ROOM_STYLE_THEATER) == 1 && roomGlowOn(c, ROOM_STYLE_GRAND_CINEMA) == 0);
    CHECK(roomGlowOn(c, 0) == 0);
    c->ambilightOn = 1;
    CHECK(roomGlowOn(c, 0) == 1);

    // Where each room hangs the picture, against the pure core and the
    // numbers upstream's comments give (theater 2.88 m, cinema 22 m wide,
    // synthwave 14 m, at 16:9)
    c->roomOverride = -1;
    for (int st = ROOM_STYLE_FIRST; st <= ROOM_STYLE_LAST; st++) {
        for (unsigned a = 0; a < sizeof(ASPECTS) / sizeof(ASPECTS[0]); a++) {
            int pcts[3] = { 100, 50, 25 };
            for (int p = 0; p < 3; p++) {
                c->roomScreen[st] = pcts[p];
                c->roomHoldingScreen = 0;
                applyRoomPlacement(c, st, ASPECTS[a], 0);
                RoomPlacement want = roomPlacement(st, roomScaleFor(-1.0f, st), ASPECTS[a],
                                                   roomScreenPercentOf(pcts[p], st));
                CHECK_NEAR(c->screenPose.position.x, want.x, 1e-5);
                CHECK_NEAR(c->screenPose.position.y, want.y, 1e-5);
                CHECK_NEAR(c->screenPose.position.z, want.z, 1e-5);
                CHECK_NEAR(c->screenWidth, want.width, 1e-5);
                CHECK(c->screenPose.orientation.w == 1.0f && c->screenPose.orientation.x == 0.0f);
                applyRoomPlacement(c, 0, ASPECTS[a], 0);
            }
        }
    }
    roomLevelsFromTable(c);
    float expectW[4] = { 0, 2.88f, 22.0f, 14.0f };
    for (int st = ROOM_STYLE_FIRST; st <= ROOM_STYLE_LAST; st++) {
        applyRoomPlacement(c, st, 9.0f / 16.0f, 0);
        CHECK_NEAR(c->screenWidth, expectW[st], 1e-3);
        applyRoomPlacement(c, 0, 9.0f / 16.0f, 0);
    }
    // A fixed room ignores a size saved for it
    c->roomScreen[ROOM_STYLE_GRAND_CINEMA] = 40;
    applyRoomPlacement(c, ROOM_STYLE_GRAND_CINEMA, 9.0f / 16.0f, 0);
    CHECK_NEAR(c->screenWidth, 22.0f, 1e-3);
    applyRoomPlacement(c, 0, 9.0f / 16.0f, 0);
    roomLevelsFromTable(c);

    // The hold and restore of the user's own placement
    XrPosef mine = { { 0.0f, 0.2588f, 0.0f, 0.9659f }, { -1.2f, 0.3f, -2.5f } };
    c->screenPose = mine;
    c->screenWidth = 2.2f;
    c->screenRadius = 9.0f;
    c->grabMode = GRAB_MOVE;
    applyRoomPlacement(c, ROOM_STYLE_THEATER, 9.0f / 16.0f, 0);
    CHECK(c->roomHoldingScreen == 1 && c->grabMode == GRAB_NONE);
    CHECK(memcmp(&c->savedScreenPose, &mine, sizeof(mine)) == 0 && c->savedScreenWidth == 2.2f
          && c->savedScreenRadius == 9.0f);
    // Moving between rooms keeps what was put aside
    applyRoomPlacement(c, ROOM_STYLE_SYNTHWAVE, 9.0f / 16.0f, 0);
    CHECK(memcmp(&c->savedScreenPose, &mine, sizeof(mine)) == 0);
    // The panel's reset while in a room: the reseeded placement waits instead
    XrPosef reseed = { { 0, 0, 0, 1 }, { 0, 0, -3.0f } };
    c->screenPose = reseed;
    c->screenWidth = 3.0f;
    applyRoomPlacement(c, ROOM_STYLE_SYNTHWAVE, 9.0f / 16.0f, 1);
    CHECK(memcmp(&c->savedScreenPose, &reseed, sizeof(reseed)) == 0 && c->savedScreenWidth == 3.0f);
    // Leaving hands it back, with a recentre in the room saved on the way out
    c->savedScreenPose = mine;
    c->savedScreenWidth = 2.2f;
    c->recentredInRoom = 1;
    c->poseDirty = 0;
    c->grabMode = GRAB_RESIZE;
    applyRoomPlacement(c, 0, 9.0f / 16.0f, 0);
    CHECK(c->roomHoldingScreen == 0 && c->grabMode == GRAB_NONE && c->poseDirty == 1 && c->recentredInRoom == 0);
    CHECK(memcmp(&c->screenPose, &mine, sizeof(mine)) == 0 && c->screenWidth == 2.2f && c->screenRadius == 9.0f);
    CHECK(c->roomPlacedStyle == 0);

    // The Room tab: the screen tab's place in a room, its rows live (size
    // only where the room lets the picture be resized), its values stored
    // under the room showing
    xrSetEnvironment(c, ENV_CELL_HOME_THEATER);
    c->cogTab = COG_TAB_SCREEN;
    CHECK(cogFace(c) == COG_FACE_ROOM);
    for (int row = 0; row < COG_ROOM_ROW_COUNT; row++) CHECK(cogRowLive(c, COG_FACE_ROOM, row));
    xrSetEnvironment(c, ENV_CELL_GRAND_CINEMA);
    CHECK(!cogRowLive(c, COG_FACE_ROOM, COG_ROOM_ROW_SIZE) && cogRowLive(c, COG_FACE_ROOM, COG_ROOM_ROW_BRIGHTNESS));
    xrSetEnvironment(c, ENV_CELL_HOME_THEATER);
    cogApplySlider(c, COG_FACE_ROOM, COG_ROOM_ROW_BRIGHTNESS, 0.95f);
    int b = c->roomBrightness[ROOM_STYLE_THEATER];
    CHECK(b > ROOM_THEATER_BRIGHTNESS);
    CHECK(xrStoreSetting(c, &s->settings, SETTING_ROOM_BRIGHTNESS, b) == 1);
    CHECK(s->settings.room[0].brightness == b && s->settings.room[1].brightness == ROOM_GRAND_CINEMA_BRIGHTNESS);
    float out[IN_SLOTS];
    memset(out, 0, sizeof(out));
    cogApplyRoomCell(c, COG_ROOM_ROW_GLOW, 0, out);
    CHECK((int)out[IN_SETTING] == SETTING_ROOM_GLOW && c->roomGlow[ROOM_STYLE_THEATER] == 0);
    CHECK(xrStoreSetting(c, &s->settings, SETTING_ROOM_GLOW, 0) == 1 && s->settings.room[0].glow == 0);
    CHECK(xrStoreSetting(c, &s->settings, SETTING_ROOM_SCREEN, 60) == 1 && s->settings.room[0].screen == 60);
    CHECK(xrStoreSetting(c, &s->settings, SETTING_ROOM_LIGHT_LEVEL, 90) == 1 && s->settings.room[0].light == 90);
    xrSetEnvironment(c, ENV_CELL_VOID);
    CHECK(xrStoreSetting(c, &s->settings, SETTING_ROOM_LIGHT_LEVEL, 10) == 0); // no room up
    // ... and they come back from config.ini
    Settings back = s->settings;
    back.environment = 3;
    XrCtx* d = calloc(1, sizeof(XrCtx));
    xrCtxDefaults(d, 1920, 1080, 60);
    xrApplySettings(d, &back);
    CHECK(roomEffective(d) == ROOM_STYLE_THEATER && d->roomBrightness[1] == b && d->roomGlow[1] == 0
          && d->roomScreen[1] == 60 && d->roomLightLevel[1] == 90 && !d->passthrough);
    Settings again = back;
    xrSettingsFromCtx(d, &again);
    CHECK(again.room[0].brightness == b && again.room[0].screen == 60);
    free(d);

    // Rows the M4 build greyed are live, supersampling included
    for (int row = 0; row <= COG_DISPLAY_SLIDER_ROW; row++) {
        CHECK(xrRowLater(c, COG_TAB_DISPLAY, row) == 0);
    }
    CHECK(xrDisplayLaterMask(c) == 0);
    // Every picker cell is live
    CHECK(c->pickerCells == ENV_CELL_COUNT);
    // The panel names the room up (UiView.room_env)
    xrSetEnvironment(c, ENV_CELL_SYNTHWAVE);
    c->cogOpen = 1;
    c->cogPose = cogPanelPose(c, &c->cogW, &c->cogH);
    scene_frame(s, NULL);
    CHECK(xrPanelsLastView(c) != NULL && xrPanelsLastView(c)->room_env == ENV_ID_SYNTHWAVE);
    c->cogOpen = 0;
    scene_free(s);
}

// ---- env ----------------------------------------------------------------------------

typedef struct {
    int cell;
    const char* name;
} Step;

static void envStep(Scene* s, int cell) {
    XrCtx* c = s->ctx;
    // What dispatchInput does with IN_PICKER_PICK
    xrSetEnvironment(c, cell);
    c->passthrough = cell == ENV_CELL_PASSTHROUGH;
    s->settings.environment = xrEnvironmentSetting(cell);
    s->settings.passthrough = c->passthrough;
    scene_frame(s, NULL);
}

static void envGroup(void) {
    Scene* s = scene_new();
    XrCtx* c = s->ctx;
    c->alphaBlendSupported = 1;
    c->focusedFrames = 5;
    c->overlayVisible = 1;
    fakeWorld(c);
    scene_frame(s, NULL);
    XrPosef mine = c->screenPose;
    float mineW = c->screenWidth;
    CHECK(mineW > 0.5f);

    // Every ordered pair of environments, both ways
    for (int from = 0; from < ENV_CELL_COUNT; from++) {
        for (int to = 0; to < ENV_CELL_COUNT; to++) {
            int seq[3] = { from, to, from };
            for (int k = 0; k < 3; k++) {
                int cell = seq[k];
                envStep(s, cell);
                int style = roomStyleForCell(cell);
                int pt = cell == ENV_CELL_PASSTHROUGH;
                CHECK(roomEffective(c) == style);
                CHECK(c->passthrough == pt);
                // Alpha blend only in passthrough, opaque in the void and a room
                CHECK(s->out.blend == (pt ? XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND : XR_ENVIRONMENT_BLEND_MODE_OPAQUE));
                // The room's layer only in a room, first, and never with passthrough
                CHECK(hasSwapchain(&s->out, FAKE_ROOM) == (style > 0));
                if (style > 0) CHECK(s->out.order[0]->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION);
                // No models over passthrough (upstream: the real hands show)
                CHECK(hasSwapchain(&s->out, FAKE_MODEL) == !pt);
                // The picture where the room hangs it, the user's own elsewhere
                if (style > 0) {
                    RoomPlacement want = roomPlacement(style, 1.0f, (float)c->videoHeight / c->videoWidth,
                                                       roomScreenPercent(c, style));
                    CHECK_NEAR(c->screenPose.position.z, want.z, 1e-5);
                    CHECK_NEAR(c->screenWidth, want.width, 1e-5);
                }
                else {
                    CHECK(memcmp(&c->screenPose, &mine, sizeof(mine)) == 0 && c->screenWidth == mineW);
                }
                // The config.ini value goes back to the same cell
                CHECK(xrCellForEnvironment(settings_environment_id(&s->settings)) == cell);
            }
        }
    }
    // The glow: the app wide switch outside a room, the room's own inside
    c->ambilightOn = 0;
    envStep(s, ENV_CELL_VOID);
    CHECK(!hasSwapchain(&s->out, FAKE_GLOW));
    envStep(s, ENV_CELL_HOME_THEATER);
    CHECK(hasSwapchain(&s->out, FAKE_GLOW));
    envStep(s, ENV_CELL_GRAND_CINEMA);
    CHECK(!hasSwapchain(&s->out, FAKE_GLOW));
    c->ambilightOn = 1;
    envStep(s, ENV_CELL_PASSTHROUGH);
    CHECK(hasSwapchain(&s->out, FAKE_GLOW));
    // The glow never stands in for a missing picture (the launcher's list)
    s->launcher.mode = LAUNCHER_PCS;
    scene_frame(s, NULL);
    CHECK(c->launcher == LAUNCHER_PCS && !hasSwapchain(&s->out, FAKE_GLOW));
    s->launcher.mode = LAUNCHER_NONE;
    scene_frame(s, NULL);
    CHECK(c->launcher == LAUNCHER_NONE && hasSwapchain(&s->out, FAKE_GLOW));
    // A head locked picture is world locked in a room
    s->settings.head_locked = 1;
    c->headLockedPref = 1;
    envStep(s, ENV_CELL_SYNTHWAVE);
    for (uint32_t i = 0; i < s->out.count; i++) {
        if (s->out.order[i]->type == XR_TYPE_COMPOSITION_LAYER_QUAD) {
            const XrCompositionLayerQuad* q = (const XrCompositionLayerQuad*)s->out.order[i];
            if (q->subImage.swapchain == SCENE_VIDEO_SWAPCHAIN) CHECK(q->space == c->localSpace);
        }
    }
    s->settings.head_locked = 0;
    c->headLockedPref = 0;
    envStep(s, ENV_CELL_VOID);
    CHECK(memcmp(&c->screenPose, &mine, sizeof(mine)) == 0);
    scene_free(s);
}

// ---- budget --------------------------------------------------------------------------

static int frameLayers(Scene* s, const char* what, int passthroughExtra) {
    scene_frame(s, "stats");
    int n = (int)s->out.count + passthroughExtra;
    printf("  %-62s %2d layers%s\n", what, n, passthroughExtra ? " (incl. passthrough)" : "");
    CHECK(n <= 16);
    return n;
}

static void toastUp(XrCtx* c) {
    noticePush(&c->notices, TOAST_3D_ON, 0);
    Notice up;
    if (noticeAdvance(&c->notices, nowNs(), 0, &up)) xrPanelsToast(c, up.kind, up.arg);
}

static void budgetGroup(void) {
    Scene* s = scene_new();
    XrCtx* c = s->ctx;
    c->alphaBlendSupported = 1;
    c->focusedFrames = 5;
    c->overlayVisible = 1;
    c->padButtonReady = 1;
    c->ambilightOn = 1;
    fakeWorld(c);
    Vec3 hand = { 0.2f, -0.3f, -0.3f }, at = { 0, -1.0f, -3.0f };
    scene_ray_to(s, hand, at);
    printf("busiest frames (SteamVR takes 16):\n");

    // The void, head locked (head aim's button too): the bar at its fullest
    s->settings.head_locked = 1;
    c->headLockedPref = 1;
    c->hoverKind = HOVER_BAR;
    toastUp(c);
    int worst = frameLayers(s, "void, head locked, bar, glow, models, stats, ray, dot, toast", 0);
    CHECK(hasSwapchain(&s->out, FAKE_GLOW) && hasSwapchain(&s->out, FAKE_MODEL));
    // The same in passthrough (no models there, its own layer instead)
    envStep(s, ENV_CELL_PASSTHROUGH);
    c->hoverKind = HOVER_BAR;
    toastUp(c);
    int n = frameLayers(s, "passthrough, head locked, bar, glow, stats, ray, dot, toast", 1);
    worst = n > worst ? n : worst;
    envStep(s, ENV_CELL_VOID);
    // Curved: the glow is drawn into the curved picture's layer
    XrCompositionLayerProjection curvedLayer;
    memset(&curvedLayer, 0, sizeof(curvedLayer));
    curvedLayer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
    c->panelCurve = 0.6f;
    c->hoverKind = HOVER_BAR;
    {
        s->live = s->settings;
        XrFrameIn in;
        memset(&in, 0, sizeof(in));
        xrDefaultPlacement(&in.distance, &in.quadWidth, &in.curvature);
        in.headLocked = 1;
        in.curved = &curvedLayer;
        xrPanelsUpdate(c, &s->live, &s->launcher, "stats", "10:42", nowNs());
        xrBuildLayers(c, &in, &s->out);
        printf("  %-62s %2u layers\n", "void, curved, bar, glow in the curve's layer, models, ray, dot", s->out.count);
        CHECK(s->out.count <= 16 && !hasSwapchain(&s->out, FAKE_GLOW));
        int curvedSeen = 0;
        for (uint32_t i = 0; i < s->out.count; i++) curvedSeen += s->out.order[i] == (void*)&curvedLayer;
        CHECK(curvedSeen == 1);
    }
    c->panelCurve = -1.0f;
    // Supersampled: the picture's quad shows the host's smaller image
    {
        XrFrameIn in;
        memset(&in, 0, sizeof(in));
        xrDefaultPlacement(&in.distance, &in.quadWidth, &in.curvature);
        in.headLocked = 1;
        in.picture = (XrSwapchain)(uintptr_t)0x7005;
        in.pictureW = 928;
        in.pictureH = 522;
        xrBuildLayers(c, &in, &s->out);
        int seen = 0;
        for (uint32_t i = 0; i < s->out.count; i++) {
            const XrCompositionLayerQuad* q = (const XrCompositionLayerQuad*)s->out.order[i];
            if (q->type == XR_TYPE_COMPOSITION_LAYER_QUAD && q->subImage.swapchain == in.picture) {
                seen++;
                CHECK(q->subImage.imageRect.extent.width == 928 && q->subImage.imageRect.extent.height == 522);
                CHECK_NEAR(q->size.width, c->screenWidth, 1e-6);
            }
        }
        CHECK(seen == 1 && !hasSwapchain(&s->out, SCENE_VIDEO_SWAPCHAIN));
    }
    s->settings.head_locked = 0;
    c->headLockedPref = 0;

    // Each room: the bar (no pill in a room), then the Room tab, the Display
    // tab, the keyboard, each with the glow, the models, stats, ray, toast
    for (int cell = ENV_CELL_HOME_THEATER; cell <= ENV_CELL_SYNTHWAVE; cell++) {
        char what[96];
        envStep(s, cell);
        c->roomGlow[roomStyleForCell(cell)] = 1;
        c->hoverKind = HOVER_BAR;
        toastUp(c);
        snprintf(what, sizeof(what), "room %d, bar, glow, models, stats, ray, dot, toast", roomEffective(c));
        n = frameLayers(s, what, 0);
        worst = n > worst ? n : worst;
        CHECK(hasSwapchain(&s->out, FAKE_ROOM));
        c->hoverKind = HOVER_NONE;
        for (int tab = COG_TAB_SCREEN; tab <= COG_TAB_DISPLAY; tab++) {
            c->cogTab = tab;
            c->cogOpen = 1;
            c->cogPose = cogPanelPose(c, &c->cogW, &c->cogH);
            toastUp(c);
            snprintf(what, sizeof(what), "room %d, %s tab, glow, models, stats, ray, dot, toast", roomEffective(c),
                     tab == COG_TAB_SCREEN ? "Room" : "Display");
            n = frameLayers(s, what, 0);
            worst = n > worst ? n : worst;
        }
        c->cogOpen = 0;
        c->panelFades[FADE_COG].level = 0.0f;
        c->kbOpen = 1;
        c->kbPose = kbPanelPose(c, &c->kbW, &c->kbH);
        c->kbHoverKey = 3;
        snprintf(what, sizeof(what), "room %d, keyboard, glow, models, stats, ray, dot", roomEffective(c));
        n = frameLayers(s, what, 0);
        worst = n > worst ? n : worst;
        c->kbOpen = 0;
        c->panelFades[FADE_KB].level = 0.0f;
    }
    printf("  worst case %d of 16\n", worst);

    // The drop order when a frame does go over (a runtime with fewer): the
    // toast first, then the models, as upstream
    envStep(s, ENV_CELL_HOME_THEATER);
    c->hoverKind = HOVER_BAR;
    toastUp(c);
    scene_frame(s, "stats");
    int full = (int)s->out.count;
    CHECK(hasSwapchain(&s->out, FAKE_MODEL));
    c->maxLayerCount = full - 1;
    toastUp(c);
    scene_frame(s, "stats");
    CHECK((int)s->out.count == full - 1 && hasSwapchain(&s->out, FAKE_MODEL));
    c->maxLayerCount = full - 2;
    toastUp(c);
    scene_frame(s, "stats");
    CHECK((int)s->out.count == full - 2 && !hasSwapchain(&s->out, FAKE_MODEL) && hasSwapchain(&s->out, FAKE_ROOM));
    printf("  over a limit of %d: the toast goes, then the models (%d -> %u)\n", full - 2, full, s->out.count);
    c->maxLayerCount = 16;

    // Swapchains: what the session makes at most, against SteamVR's 16 a
    // session: the picture, the UI atlas, passthrough's transparent layer, the
    // curved picture's layer, the room, the models, the glow (xr_session.c,
    // xr_assets.c, xr_room.c, xr_model.c, xr_world.c each count theirs in
    // swapchainsAlive and log it)
    const char* chains[] = { "picture", "UI atlas", "passthrough", "curved picture", "supersampled picture", "room",
                             "controller models", "glow" };
    int nChains = (int)(sizeof(chains) / sizeof(chains[0]));
    printf("  swapchains at most: %d of 16 (", nChains);
    for (int i = 0; i < nChains; i++) printf("%s%s", chains[i], i + 1 < nChains ? ", " : ")\n");
    CHECK(nChains <= 16);
    scene_free(s);
}

// ---- click ---------------------------------------------------------------------------

static int played;
static void player(void) { played++; }

static void clickGroup(void) {
    XrCtx* c = calloc(1, sizeof(XrCtx));
    xrCtxDefaults(c, 1920, 1080, 60);
    Settings st;
    settings_defaults(&st);
    xrApplySettings(c, &st);
    CHECK(c->clickSoundOn == st.click_sound);
    float in[IN_SLOTS];
    memset(in, 0, sizeof(in));
    xrClickPlayer = player;
    c->clickSoundOn = 1;
    CHECK(xrClickFrame(c, in) == 0 && played == 0); // nothing pressed
    in[IN_CLICK] = 1.0f;
    CHECK(xrClickFrame(c, in) == 1 && played == 1);
    // The Display tab's row switches it, and is stored
    CHECK(cogApplyOption(c, COG_OPTION_CLICK_SOUND, 0) == SETTING_CLICK_SOUND && c->clickSoundOn == 0);
    CHECK(xrClickFrame(c, in) == 0 && played == 1);
    CHECK(xrStoreSetting(c, &st, SETTING_CLICK_SOUND, 0) == 1 && st.click_sound == 0);
    CHECK(cogApplyOption(c, COG_OPTION_CLICK_SOUND, 1) == SETTING_CLICK_SOUND && c->clickSoundOn == 1);
    CHECK(cogRowLive(c, COG_TAB_DISPLAY, COG_OPTION_CLICK_SOUND));
    // No player (PulseAudio unavailable): nothing happens, nothing blocks
    xrClickPlayer = NULL;
    CHECK(xrClickFrame(c, in) == 0 && played == 1);
    // The other rows M5 brings live act on the context
    CHECK(cogApplyOption(c, COG_OPTION_CONTROLLERS, 1) == SETTING_CONTROLLER_MODEL && c->modelOn == 1);
    CHECK(cogApplyOption(c, COG_OPTION_AMBILIGHT, 0) == SETTING_AMBILIGHT && c->ambilightOn == 0);
    CHECK(cogApplyOption(c, COG_OPTION_ROOM_LIGHT, 0) == SETTING_ROOM_LIGHT && c->roomLightOn == 0);
    float before = c->ambiIntensity;
    cogApplySlider(c, COG_TAB_DISPLAY, COG_DISPLAY_SLIDER_ROW, cogRunU(0.25f));
    CHECK(c->ambiIntensity == 0.25f && before != 0.25f);
    free(c);
}

int main(int argc, char** argv) {
    const char* g = argc > 1 ? argv[1] : "all";
    int all = !strcmp(g, "all");
    if (all || !strcmp(g, "room")) roomGroup();
    if (all || !strcmp(g, "env")) envGroup();
    if (all || !strcmp(g, "budget")) budgetGroup();
    if (all || !strcmp(g, "click")) clickGroup();
    char name[32];
    snprintf(name, sizeof(name), "m5 %s", g);
    return checksDone(name);
}
