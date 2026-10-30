// Moonlight XR Native, tests/m4: the first-run card and the pad card (PLAN 3.9, 2b.6). The pure
// pieces (the card's life, the auto-hide timer as a function, the lap geometry), config.ini's
// first_run_shown and pad_card round trip, and the cards through the frame loop's glue and the
// layer builder: once per install, in the lap under the head's heading, yielding to the rest of
// the frame. GPLv3.
#include <math.h>
#include <string.h>

#include "check.h"
#include "scene.h"

#define SEC 1000000000LL
// xr_layers.c keeps this many layers back for the models, the ray, the dot and the toast
#define CARD_RESERVE_FOR_TEST 4

// ---- the pure pieces --------------------------------------------------------------------------

static void test_first_run_life(void) {
    FirstRun f;
    // already shown: never up, never marks
    firstRunInit(&f, 1);
    CHECK(!firstRunStep(&f, 1, 0, 0) && !firstRunStep(&f, 1, 0, 100 * SEC) && !firstRunTakeMark(&f));

    // not ready yet: waits, with no clock running
    firstRunInit(&f, 0);
    CHECK(!firstRunStep(&f, 0, 0, 5 * SEC) && !firstRunStep(&f, 0, 0, 500 * SEC) && !firstRunTakeMark(&f));
    // ready: up, and the mark is given once
    CHECK(firstRunStep(&f, 1, 0, 1000 * SEC));
    CHECK(firstRunTakeMark(&f) == 1 && firstRunTakeMark(&f) == 0);
    // it stays up until FIRSTRUN_SHOW_NS after it came up, whether or not ready holds
    CHECK(firstRunStep(&f, 1, 0, 1000 * SEC + 1));
    CHECK(firstRunStep(&f, 0, 0, 1000 * SEC + FIRSTRUN_SHOW_NS - 1));
    CHECK(!firstRunStep(&f, 1, 0, 1000 * SEC + FIRSTRUN_SHOW_NS));
    // and never comes back
    CHECK(!firstRunStep(&f, 1, 0, 1000 * SEC + FIRSTRUN_SHOW_NS + 1) && !firstRunStep(&f, 1, 0, 9000 * SEC));
    CHECK(!firstRunTakeMark(&f));
    CHECK(FIRSTRUN_SHOW_NS == 20 * SEC);

    // a sheet opening while it is up puts it away for good
    firstRunInit(&f, 0);
    CHECK(firstRunStep(&f, 1, 0, 10 * SEC));
    CHECK(!firstRunStep(&f, 1, 1, 12 * SEC));
    CHECK(!firstRunStep(&f, 1, 0, 13 * SEC));
    CHECK(firstRunTakeMark(&f) == 1);   // it was up: that is written down all the same

    // a sheet already open when the stream is ready: the card is skipped (the person is in the menus)
    firstRunInit(&f, 0);
    CHECK(!firstRunStep(&f, 1, 1, 1 * SEC) && !firstRunStep(&f, 1, 0, 2 * SEC) && !firstRunTakeMark(&f));
}

static void test_pad_card_timer(void) {
    CHECK(PADCARD_AUTOHIDE_NS == 4 * SEC);
    // auto: up while a pad is in use and for 4 s after the last input, then away
    CHECK(padCardVisible(PADCARD_AUTO, 1, 0));
    CHECK(padCardVisible(PADCARD_AUTO, 1, 3999999999LL));
    CHECK(!padCardVisible(PADCARD_AUTO, 1, 4 * SEC));
    CHECK(!padCardVisible(PADCARD_AUTO, 1, 19 * SEC));
    // on: stays while the pad counts as in use, whatever the age
    CHECK(padCardVisible(PADCARD_ON, 1, 0) && padCardVisible(PADCARD_ON, 1, 19 * SEC));
    // off: never
    CHECK(!padCardVisible(PADCARD_OFF, 1, 0));
    // no pad in use: never, in any mode
    for (int mode = 0; mode < PADCARD_MODES; mode++) CHECK(!padCardVisible(mode, 0, 0));
    // an unknown mode reads as auto
    CHECK(padCardVisible(9, 1, 0) && !padCardVisible(9, 1, 5 * SEC));
}

static void test_pad_live_bits(void) {
    CHECK(padStyleFor(0, 1) == PADSTYLE_XBOX && padStyleFor(1, 1) == PADSTYLE_PLAYSTATION &&
          padStyleFor(2, 1) == PADSTYLE_NINTENDO && padStyleFor(3, 1) == PADSTYLE_STEAM);
    CHECK(padStyleFor(0, 0) == PADSTYLE_GENERIC && padStyleFor(9, 1) == PADSTYLE_GENERIC && padStyleFor(-1, 1) == PADSTYLE_GENERIC);
    CHECK(!padStickLit(0, 0) && !padStickLit(3000, 0) && !padStickLit(6000, 0) && padStickLit(6600, 0) &&
          padStickLit(0, -32768) && padStickLit(-20000, 20000));
    CHECK_NEAR(padStickMag(32767, 0), 1.0, 1e-6);
    CHECK_NEAR(padStickMag(-32768, -32768), 1.0, 1e-6);   // held to full tilt
    CHECK(!padTriggerLit(0) && !padTriggerLit(12) && padTriggerLit(13) && padTriggerLit(255));

    // quantised: a wobble you cannot see is the same panel, a visible move is not, and it is stable
    PadLive a, b, qa, qb, qq;
    memset(&a, 0, sizeof a);
    a.style = PADSTYLE_STEAM;
    a.buttons = PAD_A | PAD_DOWN;
    a.lx = 10000;
    a.ly = -10000;
    a.lt = 100;
    a.touching = 1;
    a.touchX = 0.5f;
    a.touchY = 0.3f;
    a.ageNs = 3 * SEC;
    b = a;
    b.lx += 300;
    b.lt += 3;
    b.touchX += 0.004f;
    b.ageNs = 0;
    padLiveQuantise(&a, &qa);
    padLiveQuantise(&b, &qb);
    CHECK(qa.buttons == qb.buttons && qa.lx == qb.lx && qa.lt == qb.lt && qa.touchX == qb.touchX);
    CHECK(qa.ageNs == 0 && qb.ageNs == 0 && qa.style == PADSTYLE_STEAM && qa.touching == 1);
    padLiveQuantise(&qa, &qq);
    CHECK(qq.lx == qa.lx && qq.ly == qa.ly && qq.lt == qa.lt && qq.touchX == qa.touchX && qq.touchY == qa.touchY);
    b = a;
    b.lx += 4000;
    padLiveQuantise(&b, &qb);
    CHECK(qb.lx != qa.lx);
    b = a;
    b.touching = 0;
    padLiveQuantise(&b, &qb);
    CHECK(qb.touchX == 0.0f && qb.touchY == 0.0f);
    // both ends of the range stay in range
    b = a;
    b.lx = -32768;
    b.ly = 32767;
    b.lt = 255;
    padLiveQuantise(&b, &qb);
    CHECK(qb.lx >= -32768 && qb.lx <= 32768 && qb.ly <= 32768 && qb.lt <= 256 && qb.lt >= 240);
}

static void test_lap_geometry(void) {
    float pos[3], tilt;
    padCardLap(0.0f, pos, &tilt);
    CHECK_NEAR(pos[0], 0.0, 1e-6);
    CHECK_NEAR(pos[1], -0.45, 1e-6);
    CHECK_NEAR(pos[2], -0.35, 1e-6);
    // the face looks at the eyes: the normal (0, -sin t, cos t) points up and back to the head
    float nx = 0.0f, ny = -sinf(tilt), nz = cosf(tilt);
    float len = sqrtf(pos[0] * pos[0] + pos[1] * pos[1] + pos[2] * pos[2]);
    CHECK_NEAR(-(pos[0] * nx + pos[1] * ny + pos[2] * nz) / len, 1.0, 1e-5);
    CHECK(tilt < 0.0f);
    // turning left 90 degrees moves it to the left, the same distance
    padCardLap((float)M_PI * 0.5f, pos, &tilt);
    CHECK_NEAR(pos[0], -0.35, 1e-5);
    CHECK_NEAR(pos[1], -0.45, 1e-6);
    CHECK_NEAR(pos[2], 0.0, 1e-5);
    padCardLap((float)M_PI, pos, &tilt);
    CHECK_NEAR(pos[2], 0.35, 1e-5);
}

// ---- config.ini ---------------------------------------------------------------------------------

static void test_settings_round_trip(void) {
    Settings s;
    settings_defaults(&s);
    CHECK(s.first_run_shown == 0 && s.pad_card == PADCARD_AUTO);
    CHECK(settings_find("vr.first_run_shown") != NULL && settings_find("vr.pad_card") != NULL);

    // the file as the VR client writes it, and back
    s.first_run_shown = 1;
    s.pad_card = PADCARD_OFF;
    char* text = settings_to_text(&s);
    CHECK(text != NULL && strstr(text, "\nfirst_run_shown = true") != NULL && strstr(text, "\npad_card = off") != NULL);
    Settings back;
    settings_defaults(&back);
    SettingsLoadInfo info;
    memset(&info, 0, sizeof info);
    settings_from_text(&back, text, &info);
    CHECK(back.first_run_shown == 1 && back.pad_card == PADCARD_OFF && info.bad == 0);
    free(text);

    // a default file has them commented out, and a file that says false brings the card back
    settings_defaults(&s);
    text = settings_to_text(&s);
    CHECK(strstr(text, "\n#first_run_shown = false") != NULL && strstr(text, "\n#pad_card = auto") != NULL);
    free(text);
    settings_defaults(&back);
    settings_from_text(&back, "[vr]\nfirst_run_shown = true\npad_card = on\n", &info);
    CHECK(back.first_run_shown == 1 && back.pad_card == PADCARD_ON);
    settings_from_text(&back, "[vr]\nfirst_run_shown = false\npad_card = auto\n", &info);
    CHECK(back.first_run_shown == 0 && back.pad_card == PADCARD_AUTO);
    // a name it does not know is left, and counted
    memset(&info, 0, sizeof info);
    settings_from_text(&back, "[vr]\npad_card = sometimes\n", &info);
    CHECK(back.pad_card == PADCARD_AUTO && info.bad == 1);
}

// ---- through the frame loop ------------------------------------------------------------------------

static const XrCompositionLayerQuad* find_quad(Scene* s, int slot) {
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

static void test_first_run_through_the_frame(void) {
    Scene* s = scene_new();
    XrCtx* c = s->ctx;
    int64_t t = 1000 * SEC;
    // an install that has not shown it: up on the first frame of a stream, marked once
    CHECK(!s->settings.first_run_shown && c->firstRun.armed);
    CHECK(xrCardsFrame(c, t) == 1 && c->firstRunUp);
    CHECK(xrCardsFrame(c, t + SEC) == 0 && c->firstRunUp);
    // its art is drawn when it is up, and the layer follows once the art is there
    c->firstRunReady = 0;
    scene_frame(s, NULL);
    CHECK(c->firstRunReady == 1 && find_quad(s, UI_PANEL_FIRSTRUN) != NULL);
    // in front of the picture, facing the viewer, not behind it
    const XrCompositionLayerQuad* q = find_quad(s, UI_PANEL_FIRSTRUN);
    if (q) {
        CHECK(q->pose.position.z < -0.3f);   // out in front of the eyes, near the picture
        CHECK_NEAR(q->size.width / q->size.height, (double)FIRSTRUN_TEX_W / FIRSTRUN_TEX_H, 1e-4);
        CHECK(q->size.width > 0.0f);
    }
    // a sheet opening takes it down at once, and it does not return
    c->cogOpen = 1;
    CHECK(xrCardsFrame(c, t + 2 * SEC) == 0 && !c->firstRunUp);
    c->cogOpen = 0;
    CHECK(xrCardsFrame(c, t + 3 * SEC) == 0 && !c->firstRunUp);
    scene_frame(s, NULL);
    CHECK(find_quad(s, UI_PANEL_FIRSTRUN) == NULL);

    // by itself after 20 s
    scene_reset(s);
    xrCardsApplySettings(c, &s->settings);
    CHECK(xrCardsFrame(c, t) == 1 && xrCardsFrame(c, t + 19 * SEC) == 0 && c->firstRunUp);
    CHECK(xrCardsFrame(c, t + 20 * SEC) == 0 && !c->firstRunUp);

    // not before there is a picture to put it over: a stream that is not up, the launcher's lists, the session out of focus
    xrCardsApplySettings(c, &s->settings);
    c->streaming = 0;
    CHECK(xrCardsFrame(c, t) == 0 && !c->firstRunUp);
    c->streaming = 1;
    c->launcher = LAUNCHER_PCS;
    CHECK(xrCardsFrame(c, t) == 0 && !c->firstRunUp);
    c->launcher = LAUNCHER_NONE;
    c->sessionState = XR_SESSION_STATE_VISIBLE;
    CHECK(xrCardsFrame(c, t) == 0 && !c->firstRunUp);
    c->sessionState = XR_SESSION_STATE_FOCUSED;
    CHECK(xrCardsFrame(c, t) == 1 && c->firstRunUp);

    // the flag written, the card never comes up again: a new session reading it
    Settings written = s->settings;
    written.first_run_shown = 1;
    xrCardsApplySettings(c, &written);
    CHECK(xrCardsFrame(c, t + 100 * SEC) == 0 && !c->firstRunUp);
    scene_free(s);
}

static Vec3 quad_normal(const XrCompositionLayerQuad* q) {
    Vec3 back = { 0.0f, 0.0f, 1.0f };
    return quatRotate(q->pose.orientation, back);
}

static void test_pad_card_through_the_frame(void) {
    Scene* s = scene_new();
    XrCtx* c = s->ctx;
    // no gamepad in use: the card is away in every mode
    for (int mode = 0; mode < PADCARD_MODES; mode++) {
        c->padCardMode = mode;
        xrCardsFrame(c, 5000 * SEC);
        CHECK(!c->padCardUp);
    }
    c->padCardMode = PADCARD_AUTO;

    // up, with the pad as the card should draw it
    c->padCardUp = 1;
    c->padLive.present = 1;
    c->padLive.style = PADSTYLE_PLAYSTATION;
    c->padLive.buttons = PAD_A;
    c->padCardReady = 0;
    // the head, 1.5 m up and a metre aside, facing -z; the card is in the lap, 0.45 m down and 0.35 m ahead
    c->headPoseValid = 1;
    c->headPose.orientation = (XrQuaternionf){ 0, 0, 0, 1 };
    c->headPose.position = (XrVector3f){ 1.0f, 1.5f, -2.0f };
    scene_frame(s, NULL);
    CHECK(c->padCardReady == 1);
    const XrCompositionLayerQuad* q = find_quad(s, UI_PANEL_PADCARD);
    CHECK(q != NULL);
    if (q) {
        CHECK_NEAR(q->pose.position.x, 1.0, 1e-4);
        CHECK_NEAR(q->pose.position.y, 1.5 - PADCARD_DOWN_M, 1e-4);
        CHECK_NEAR(q->pose.position.z, -2.0 - PADCARD_FWD_M, 1e-4);
        CHECK_NEAR(q->size.width, PADCARD_WIDTH_M, 1e-5);
        CHECK_NEAR(q->size.width / q->size.height, (double)PADCARD_TEX_W / PADCARD_TEX_H, 1e-4);
        // facing the eyes
        Vec3 n = quad_normal(q);
        Vec3 toHead = { c->headPose.position.x - q->pose.position.x, c->headPose.position.y - q->pose.position.y,
                        c->headPose.position.z - q->pose.position.z };
        float len = sqrtf(vecDot(toHead, toHead));
        CHECK_NEAR(vecDot(n, toHead) / len, 1.0, 1e-4);
    }

    // the head turns left 90 degrees and looks down 50: the card goes round with the heading and stays in the lap
    Vec3 yAxis = { 0, 1, 0 }, xAxis = { 1, 0, 0 };
    XrQuaternionf yaw = axisAngleQuat(yAxis, (float)M_PI * 0.5f);
    XrQuaternionf pitch = axisAngleQuat(xAxis, -0.87f);
    c->headPose.orientation = quatNorm(quatMul(yaw, pitch));
    scene_frame(s, NULL);
    q = find_quad(s, UI_PANEL_PADCARD);
    CHECK(q != NULL);
    if (q) {
        CHECK_NEAR(q->pose.position.x, 1.0 - PADCARD_FWD_M, 1e-3);
        CHECK_NEAR(q->pose.position.y, 1.5 - PADCARD_DOWN_M, 1e-3);
        CHECK_NEAR(q->pose.position.z, -2.0, 1e-3);
        Vec3 n = quad_normal(q);
        Vec3 toHead = { c->headPose.position.x - q->pose.position.x, c->headPose.position.y - q->pose.position.y,
                        c->headPose.position.z - q->pose.position.z };
        CHECK_NEAR(vecDot(n, toHead) / sqrtf(vecDot(toHead, toHead)), 1.0, 1e-3);
    }
    // looking straight down (and up): the heading comes from the head's up, and still holds
    for (int k = 0; k < 2; k++) {
        XrQuaternionf steep = axisAngleQuat(xAxis, k == 0 ? -1.5607f : 1.5607f);   // 89.4 degrees
        c->headPose.orientation = quatNorm(quatMul(yaw, steep));
        scene_frame(s, NULL);
        q = find_quad(s, UI_PANEL_PADCARD);
        CHECK(q != NULL);
        if (q) {
            CHECK_NEAR(q->pose.position.x, 1.0 - PADCARD_FWD_M, 2e-3);
            CHECK_NEAR(q->pose.position.z, -2.0, 2e-3);
        }
    }
    // the head not located: the card hangs in the view space, the same offset from the eyes
    c->headPoseValid = 0;
    scene_frame(s, NULL);
    q = find_quad(s, UI_PANEL_PADCARD);
    CHECK(q != NULL);
    if (q) {
        CHECK_NEAR(q->pose.position.x, 0.0, 1e-5);
        CHECK_NEAR(q->pose.position.y, -PADCARD_DOWN_M, 1e-5);
        CHECK_NEAR(q->pose.position.z, -PADCARD_FWD_M, 1e-5);
    }

    // away when the frame says so
    c->padCardUp = 0;
    scene_frame(s, NULL);
    CHECK(find_quad(s, UI_PANEL_PADCARD) == NULL);
    scene_free(s);
}

// Every layer of `before` is in `after`, in the same order
static int keeps_all(const XrFrameOut* after, const XrFrameOut* before) {
    unsigned j = 0;
    for (unsigned i = 0; i < after->count && j < before->count; i++) {
        if (after->order[i] == before->order[j]) j++;
    }
    return j == before->count;
}

// The cards take the room the rest of the frame leaves and no more: the frames that were full are
// as full as they were, and the models, ray, dot and toast keep their places
static void test_cards_yield(void) {
    Scene* s = scene_new();
    XrCtx* c = s->ctx;
    s->settings.head_locked = 1;
    c->headLockedPref = 1;
    c->overlayVisible = 1;
    c->hoverKind = HOVER_BAR;
    Vec3 hand = { 0.2f, -0.3f, -0.3f }, at = { 0, -1.0f, -3.0f };
    scene_ray_to(s, hand, at);
    noticePush(&c->notices, TOAST_3D_ON, 0);
    Notice up;
    if (noticeAdvance(&c->notices, nowNs(), 0, &up)) xrPanelsToast(c, up.kind, up.arg);
    scene_frame(s, "stats");
    unsigned full = s->out.count;
    XrFrameOut before = s->out;
    c->firstRunUp = 1;
    c->padCardUp = 1;
    c->padLive.present = 1;
    c->headPoseValid = 1;
    c->headPose.orientation = (XrQuaternionf){ 0, 0, 0, 1 };
    scene_frame(s, "stats");
    scene_frame(s, "stats");   // the art lands (drawn in the first, ready for the second)
    printf("the fullest bar frame: %u layers, with both cards wanted: %u\n", full, s->out.count);
    CHECK(s->out.count + 1 <= 16);
    CHECK(s->out.count <= full + 2);
    // every layer of the frame without the cards is still there, in the same order (the models,
    // ray, dot and toast after the cards included), and the runtime's limit holds
    CHECK(keeps_all(&s->out, &before));
    CHECK(s->out.count <= (unsigned)c->maxLayerCount);
    // tighter limits: no room at all draws neither card and leaves the frame as it was; a little room
    // never costs a later layer its place (a card keeps CARD_RESERVE_FOR_TEST layers back for them)
    {
        int cap = c->maxLayerCount;
        for (int extra = 0; extra <= CARD_RESERVE_FOR_TEST; extra++) {
            c->maxLayerCount = (int)full + extra;
            scene_frame(s, "stats");
            CHECK(keeps_all(&s->out, &before));
            CHECK(s->out.count <= (unsigned)c->maxLayerCount);
            if (extra == 0) {
                CHECK(s->out.count == full);
                CHECK(find_quad(s, UI_PANEL_FIRSTRUN) == NULL && find_quad(s, UI_PANEL_PADCARD) == NULL);
            }
        }
        c->maxLayerCount = cap;
    }
    // a frame with room takes both
    c->hoverKind = HOVER_NONE;
    c->overlayVisible = 0;
    scene_frame(s, NULL);
    scene_frame(s, NULL);
    CHECK(find_quad(s, UI_PANEL_FIRSTRUN) != NULL && find_quad(s, UI_PANEL_PADCARD) != NULL);
    CHECK(s->out.count + 1 <= 16);
    scene_free(s);
}

int main(void) {
    test_first_run_life();
    test_pad_card_timer();
    test_pad_live_bits();
    test_lap_geometry();
    test_settings_round_trip();
    test_first_run_through_the_frame();
    test_pad_card_through_the_frame();
    test_cards_yield();
    return checksDone("m4 cards");
}
