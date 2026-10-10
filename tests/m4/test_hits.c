// Moonlight XR Native, tests/m4: the glue between the ported hit logic
// (xr_input.c's hover pass, xr_ui.c's rows and liveness, xr_layout's zones)
// and lane U's panels (their drawn hit regions): every bar button, every row
// and cell of every settings tab, the exit prompt, every key, and the
// launcher's list are reached by a ray at them, and land where the panel art
// says they are. GPLv3.
#include "check.h"
#include "scene.h"

// A ray from the eye at a point
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

static const Vec3 EYE = { 0.0f, 0.0f, 0.0f };
static const Vec3 HAND = { 0.2f, -0.3f, -0.25f };

static void barButtons(Scene* s) {
    XrCtx* c = s->ctx;
    static const int SLOTS[] = { BAR_SLOT_ENV, BAR_SLOT_COG, BAR_SLOT_KB, BAR_SLOT_EXIT, BAR_SLOT_RAY, BAR_SLOT_AIM,
                                 BAR_SLOT_PAD, BAR_SLOT_STEREO };
    static const int HOVERS[] = { HOVER_ENVBUTTON, HOVER_COGBUTTON, HOVER_KBBUTTON, HOVER_EXITBUTTON, HOVER_RAYBUTTON,
                                  HOVER_AIMBUTTON, HOVER_PADBUTTON, HOVER_STEREOBUTTON };
    for (int headLocked = 0; headLocked < 2; headLocked++) {
        c->headLockedPref = headLocked;
        for (int i = 0; i < 8; i++) {
            BarFrame f = barFrame(c);
            Vec3 local;
            float side;
            barSlotPlacement(SLOTS[i], f.width, f.height, &local, &side);
            Vec3 off = quatRotate(f.pose.orientation, local);
            Vec3 p = { f.pose.position.x + off.x, f.pose.position.y + off.y, f.pose.position.z + off.z };
            // From the eye and from a hand off to the side
            for (int from = 0; from < 2; from++) {
                int hover = xrHoverAt(c, aimAt(from ? HAND : EYE, p), NULL, NULL, NULL);
                int shown = SLOTS[i] == BAR_SLOT_AIM ? headLocked
                          : SLOTS[i] == BAR_SLOT_STEREO ? 0 : 1; // pad shown since M3
                if (shown) {
                    CHECK(hover == HOVERS[i]);
                    if (hover != HOVERS[i]) fprintf(stderr, "  slot %d head locked %d from %d: hover %d\n", SLOTS[i], headLocked, from, hover);
                }
                else {
                    // Not on the bar (3D until M6): the ray falls through to the bar's halo
                    CHECK(hover != HOVERS[i]);
                }
            }
        }
    }
    c->headLockedPref = 0;
    // The pill between the env and cog buttons, the picture and a corner
    BarFrame f = barFrame(c);
    Vec3 pill = { f.pose.position.x, f.pose.position.y - (f.height * 0.5f + f.width * BAR_DROP_FRAC), f.pose.position.z };
    CHECK(xrHoverAt(c, aimAt(EYE, pill), NULL, NULL, NULL) == HOVER_BAR);
    float u, v;
    CHECK(xrHoverAt(c, aimAt(EYE, onPlane(c->screenPose, c->screenWidth, c->screenWidth * 9 / 16, 0.5f, 0.5f)),
                    &u, &v, NULL) == HOVER_SCREEN);
    CHECK_NEAR(u, 0.5, 1e-3);
    CHECK_NEAR(v, 0.5, 1e-3);
    int corner = -1;
    float side = cornerSide(c);
    float h = c->screenWidth * 9 / 16;
    Vec3 br = onPlane(c->screenPose, c->screenWidth + side * 0.6f, h + side * 0.6f, 1.0f, 1.0f);
    CHECK(xrHoverAt(c, aimAt(EYE, br), NULL, NULL, &corner) == HOVER_CORNER);
    CHECK(corner == 3);
}

// Every row of every tab: the ray at the row's middle (and at each cell) is
// on the row xr_ui.c's search finds and on the hit lane U drew there
static void cogRows(Scene* s) {
    XrCtx* c = s->ctx;
    for (int tab = 0; tab < COG_TAB_COUNT; tab++) {
        scene_reset(s);
        c->cogOpen = 1;
        c->cogTab = tab;
        c->cogPose = cogPanelPose(c, &c->cogW, &c->cogH);
        scene_frame(s, NULL);
        const UiView* view = xrPanelsLastView(c);
        CHECK(view != NULL && view->cog_tab == tab);
        static uint8_t px[COG_TEX_W * COG_TEX_H * 4];
        UiCanvas cv;
        ui_cv_init(&cv, px, COG_TEX_W, COG_TEX_H);
        ui_cv_clear(&cv);
        UiHits hits;
        ui_hits_clear(&hits);
        ui_draw_panel(UI_PANEL_COG, &cv, view, &hits);
        int face = cogFace(c);
        // The tabs
        for (int t = 0; t < COG_TAB_COUNT; t++) {
            float pu = (t + 0.5f) / COG_TAB_COUNT, pv = COG_TAB_BAR_B * 0.5f;
            int i = ui_hits_at(&hits, pu, pv);
            CHECK(i >= 0 && hits.hit[i].kind == UI_HIT_TAB && hits.hit[i].a == t);
            CHECK((int)(pu * COG_TAB_COUNT) == t); // updateCogPanel's own sums
        }
        for (int row = 0; row < cogTabRowCount(face); row++) {
            int live = cogRowLive(c, face, row);
            int track = cogRowIsTrack(face, row);
            float pv = cogRowV(face, row);
            int cells = track ? 1 : cogRowCells(face, row);
            for (int cell = 0; cell < cells; cell++) {
                float pu = track ? (COG_RUN_L + COG_RUN_R) * 0.5f
                                 : COG_TRACK_L + (cell + 0.5f) * (COG_TRACK_R - COG_TRACK_L) / cells;
                // the ray through the panel's plane lands there
                Vec3 p = onPlane(c->cogPose, c->cogW, c->cogH, pu, pv);
                float u = -1, v = -1;
                CHECK(screenProject(aimAt(HAND, p), c->cogPose, c->cogW, c->cogH, 0.0f, 0, &u, &v));
                CHECK_NEAR(u, pu, 1e-3);
                CHECK_NEAR(v, pv, 1e-3);
                // xr_ui.c's row search, as updateCogPanel runs it
                int found = -1;
                for (int r = 0; r < cogTabRowCount(face); r++) {
                    if (cogRowLive(c, face, r) && fabsf(v - cogRowV(face, r)) < cogRowHalf(face)) {
                        found = r;
                        break;
                    }
                }
                CHECK(found == (live ? row : -1));
                // lane U's hit at the same point
                int i = ui_hits_at(&hits, u, v);
                CHECK(i >= 0);
                if (i < 0) {
                    fprintf(stderr, "  tab %d row %d cell %d: no hit\n", tab, row, cell);
                    continue;
                }
                const UiHit* hit = &hits.hit[i];
                CHECK(hit->a == row);
                CHECK(hit->enabled == live);
                if (track) {
                    CHECK(hit->kind == UI_HIT_TRACK_RUN);
                    CHECK(cogTrackPart(u) == TRACK_PART_RUN);
                }
                else {
                    CHECK(hit->kind == UI_HIT_CELL);
                    if (live) {
                        CHECK(hit->b == cell);
                        CHECK(cogCellAt(u, cells) == cell);
                    }
                }
                if (hit->a != row || hit->enabled != live)
                    fprintf(stderr, "  tab %d row %d cell %d: hit kind %d a %d b %d enabled %d, live %d\n", tab, row,
                            cell, hit->kind, hit->a, hit->b, hit->enabled, live);
            }
            // The step buttons at a track's ends
            if (track && live) {
                float down = COG_TRACK_L + COG_CHEVRON_W * 0.5f, up = COG_TRACK_R - COG_CHEVRON_W * 0.5f;
                int i = ui_hits_at(&hits, down, pv);
                CHECK(i >= 0 && hits.hit[i].kind == UI_HIT_TRACK_DOWN && cogTrackPart(down) == TRACK_PART_DOWN);
                i = ui_hits_at(&hits, up, pv);
                CHECK(i >= 0 && hits.hit[i].kind == UI_HIT_TRACK_UP && cogTrackPart(up) == TRACK_PART_UP);
            }
        }
        // The reset button and the About tab's two
        if (face == COG_TAB_SCREEN || face == COG_TAB_3D || face == COG_TAB_PICTURE) {
            int i = ui_hits_at(&hits, (COG_RESET_L + COG_RESET_R) * 0.5f, (COG_RESET_T + COG_RESET_B) * 0.5f);
            CHECK(i >= 0 && hits.hit[i].kind == UI_HIT_BUTTON && hits.hit[i].a == UI_BTN_RESET);
        }
        if (face == COG_TAB_ABOUT) {
            float pu = (COG_REPORT_L + COG_REPORT_R) * 0.5f, pv = (COG_REPORT_T + COG_REPORT_B) * 0.5f;
            int i = ui_hits_at(&hits, pu, pv);
            CHECK(i >= 0 && hits.hit[i].a == UI_BTN_REPORT && cogReportButtonAt(pu, pv));
            pu = (COG_KOFI_L + COG_KOFI_R) * 0.5f;
            pv = (COG_KOFI_T + COG_KOFI_B) * 0.5f;
            i = ui_hits_at(&hits, pu, pv);
            CHECK(i >= 0 && hits.hit[i].a == UI_BTN_KOFI && cogKofiButtonAt(pu, pv));
        }
    }
}

static void exitAndKeys(Scene* s) {
    XrCtx* c = s->ctx;
    scene_reset(s);
    UiView v;
    ui_view_init(&v, &s->settings);
    static uint8_t px[KB_TEX_W * KB_TEX_H * 4];
    UiCanvas cv;
    UiHits hits;
    // the exit prompt's two buttons
    ui_cv_init(&cv, px, EXIT_TEX_W, EXIT_TEX_H);
    ui_hits_clear(&hits);
    ui_draw_panel(UI_PANEL_EXIT, &cv, &v, &hits);
    float pv = (EXIT_BTN_T + EXIT_BTN_B) * 0.5f;
    float pu = (EXIT_EXIT_L + EXIT_EXIT_R) * 0.5f;
    int i = ui_hits_at(&hits, pu, pv);
    CHECK(exitPromptZone(pu, pv) == EXIT_ZONE_EXIT && i >= 0 && hits.hit[i].a == UI_BTN_EXIT_CONFIRM);
    pu = (EXIT_CANCEL_L + EXIT_CANCEL_R) * 0.5f;
    i = ui_hits_at(&hits, pu, pv);
    CHECK(exitPromptZone(pu, pv) == EXIT_ZONE_CANCEL && i >= 0 && hits.hit[i].a == UI_BTN_EXIT_CANCEL);
    // every key of every sheet: the context's layout (handed over at
    // xrPanelsCreate) and kbKeyAt agree with the drawn keys
    c->kbOpen = 1;
    for (int sheet = 0; sheet < KB_STATE_COUNT; sheet++) {
        c->kbState = sheet;
        v.kb_sheet = sheet;
        ui_cv_init(&cv, px, KB_TEX_W, KB_TEX_H);
        ui_hits_clear(&hits);
        ui_draw_panel(UI_PANEL_KEYBOARD, &cv, &v, &hits);
        int keys = 0;
        for (int k = 0; k < c->kbKeyCount; k++) {
            const float* r = &c->kbKeyRects[k * 4];
            float ku = (r[0] + r[2]) * 0.5f, kv = (r[1] + r[3]) * 0.5f;
            int at = kbKeyAt(c, ku, kv);
            int hi = ui_hits_at(&hits, ku, kv);
            if (c->kbCodes[sheet][k] == 0) {
                CHECK(at == -1);
                continue;
            }
            keys++;
            CHECK(at == k);
            CHECK(hi >= 0 && hits.hit[hi].kind == UI_HIT_KEY && hits.hit[hi].a == k
                  && hits.hit[hi].b == c->kbCodes[sheet][k]);
        }
        CHECK(keys > 30);
    }
}

// The launcher's list stands in the picture's place: a point on an item of
// the drawn list, put on the picture and back, is that item
static void launcherList(Scene* s) {
    XrCtx* c = s->ctx;
    scene_reset(s);
    for (int i = 0; i < 6; i++) {
        memset(&s->pcs[i], 0, sizeof(s->pcs[i]));
        snprintf(s->pcs[i].name, sizeof(s->pcs[i].name), "PC %d", i);
        s->pcs[i].state = UI_PC_ONLINE;
        s->pcs[i].paired = 1;
    }
    s->launcher.mode = LAUNCHER_PCS;
    s->launcher.pcs = s->pcs;
    s->launcher.pcCount = 6;
    scene_frame(s, NULL);
    CHECK(c->launcher == LAUNCHER_PCS);
    const UiHits* hits = xrPanelsLauncherHits(c);
    CHECK(hits != NULL && hits->count > 0);
    int items = 0;
    float pictureAspect = (float)c->videoWidth / c->videoHeight;
    for (int i = 0; hits && i < hits->count; i++) {
        const UiHit* hit = &hits->hit[i];
        float lu = (hit->u0 + hit->u1) * 0.5f, lv = (hit->v0 + hit->v1) * 0.5f;
        // the list on the picture: as large as fits, centred
        float sw = c->launcherAspect < pictureAspect ? c->launcherAspect / pictureAspect : 1.0f;
        float sh = c->launcherAspect < pictureAspect ? 1.0f : pictureAspect / c->launcherAspect;
        float u = (lu - 0.5f) * sw + 0.5f, v = (lv - 0.5f) * sh + 0.5f;
        // and through the hover pass
        Vec3 p = onPlane(c->screenPose, c->screenWidth, c->screenWidth / pictureAspect, u, v);
        float hu, hv;
        int hover = xrHoverAt(c, aimAt(EYE, p), &hu, &hv, NULL);
        CHECK(hover == HOVER_SCREEN);
        float bu, bv;
        CHECK(launcherListUv(c, hu, hv, &bu, &bv));
        int got = xrPanelsLauncherHit(c, bu, bv);
        if (hit->enabled) {
            CHECK(got >= 0 && hits->hit[got].kind == hit->kind && hits->hit[got].a == hit->a);
        }
        if (hit->kind == UI_HIT_LIST_ITEM) items++;
    }
    CHECK(items == UI_LIST_ROWS);
}

// The curved picture: what mlxr_pass_panel draws (panel.vert's surface under
// xrCurveShape and xrCurveMvp) is the surface the ray's hits are worked out
// on (screenPoint/screenProject, curved), at every point
static void curvedPicture(Scene* s) {
    XrCtx* c = s->ctx;
    scene_reset(s);
    c->cylinderSupported = 1;
    c->panelCurve = 0.6f;
    XrVector3f p = c->screenPose.position;
    float d = sqrtf(p.x * p.x + p.y * p.y + p.z * p.z);
    c->screenRadius = d * (1.0f + 3.0f * (1.0f - c->panelCurve));
    float W, H, R;
    xrCurveShape(c, &W, &H, &R);
    float h = c->screenWidth * (float)c->videoHeight / (float)c->videoWidth;
    XrFovf fov = { -0.8f, 0.8f, 0.8f, -0.8f };
    XrPosef eye = { { 0, 0, 0, 1 }, { 0.03f, 0.0f, 0.0f } };
    float mvp[16];
    xrCurveMvp(fov, eye, c->screenPose, mvp);
    float proj[16], view[16], vp[16];
    projectionFromFov(proj, fov, 0.05f, 100.0f);
    viewFromPose(view, eye);
    matMul(vp, proj, view);
    for (int iy = 0; iy <= 4; iy++) {
        for (int ix = 0; ix <= 8; ix++) {
            float u = ix / 8.0f, v = iy / 4.0f;
            // panel.vert
            float a = (u - 0.5f) * W / R;
            float local[4] = { R * sinf(a), (0.5f - v) * H, R * (1.0f - cosf(a)), 1.0f };
            float clip[4] = { 0, 0, 0, 0 };
            for (int r = 0; r < 4; r++)
                for (int k = 0; k < 4; k++) clip[r] += mvp[k * 4 + r] * local[k];
            // the hit side's surface, through the plain projection
            Vec3 P = screenPoint(u, v, c->screenPose, c->screenWidth, h, c->screenRadius, 1);
            float wp[4] = { P.x, P.y, P.z, 1.0f }, clip2[4] = { 0, 0, 0, 0 };
            for (int r = 0; r < 4; r++)
                for (int k = 0; k < 4; k++) clip2[r] += vp[k * 4 + r] * wp[k];
            CHECK_NEAR(clip[0] / clip[3], clip2[0] / clip2[3], 1e-4);
            CHECK_NEAR(clip[1] / clip[3], clip2[1] / clip2[3], 1e-4);
            // and the ray at it lands back on (u, v)
            float hu = -1, hv = -1;
            CHECK(screenProject(aimAt(EYE, P), c->screenPose, c->screenWidth, h, c->screenRadius, 1, &hu, &hv));
            CHECK_NEAR(hu, u, 1e-3);
            CHECK_NEAR(hv, v, 1e-3);
        }
    }
    // The bar follows the curved picture, and its buttons are still hit
    BarFrame f = barFrame(c);
    CHECK(f.curved);
    Vec3 local;
    float side;
    barSlotPlacement(BAR_SLOT_EXIT, f.width, f.height, &local, &side);
    curveLocal(&local, f.radius, f.curved, NULL);
    Vec3 off = quatRotate(f.pose.orientation, local);
    Vec3 b = { f.pose.position.x + off.x, f.pose.position.y + off.y, f.pose.position.z + off.z };
    CHECK(xrHoverAt(c, aimAt(EYE, b), NULL, NULL, NULL) == HOVER_EXITBUTTON);
    // The launcher's list is flat whatever the curve
    c->launcher = LAUNCHER_PCS;
    CHECK(effectiveCurvature(c) == 0.0f && xrCurveSetting(c) > 0.5f);
    c->launcher = LAUNCHER_NONE;
    c->panelCurve = -1.0f;
}

int main(void) {
    Scene* s = scene_new();
    curvedPicture(s);
    barButtons(s);
    cogRows(s);
    exitAndKeys(s);
    launcherList(s);
    scene_free(s);
    return checksDone("m4 hits");
}
