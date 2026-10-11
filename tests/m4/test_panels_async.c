// Moonlight XR Native, tests/m4: the panels drawn on the worker thread
// (xrPanelsSetAsync) land the same pixels, launcher hits and flags as the
// panels drawn inline, state by state; a burst of changes without a flush
// ends on the newest state; and the frame loop's update never waits for a
// drawing. GPLv3.
#include "check.h"
#include "scene.h"

#include <time.h>

static size_t atlasBytes(void) {
    return (size_t)UI_ATLAS_W * UI_ATLAS_H * 4;
}

static void fillLists(Scene* s) {
    for (int i = 0; i < 6; i++) {
        memset(&s->pcs[i], 0, sizeof(s->pcs[i]));
        snprintf(s->pcs[i].name, sizeof(s->pcs[i].name), "PC %d", i);
        s->pcs[i].state = i % 3 == 0 ? UI_PC_OFFLINE : UI_PC_ONLINE;
        s->pcs[i].paired = i != 2;
    }
    for (int i = 0; i < 7; i++) {
        memset(&s->apps[i], 0, sizeof(s->apps[i]));
        snprintf(s->apps[i].name, sizeof(s->apps[i].name), "App %d", i);
        s->apps[i].running = i == 1;
    }
}

// One state of the UI, the same on both scenes
static void setState(Scene* s, int step) {
    XrCtx* c = s->ctx;
    scene_reset(s);
    fillLists(s);
    switch (step) {
    case 0: break; // the bar's buttons only
    case 1: case 2: case 3: case 4: case 5:
        c->cogOpen = 1;
        c->panelFades[FADE_COG].level = 1.0f;
        c->cogTab = step - 1;
        break;
    case 6:
        c->cogOpen = 1;
        c->panelFades[FADE_COG].level = 1.0f;
        c->cogTab = COG_TAB_DISPLAY;
        c->cogHoverSlider = 0;
        break;
    case 7: c->kbOpen = 1; break;
    case 8:
        c->exitConfirmOpen = 1;
        c->panelFades[FADE_EXIT].level = 1.0f;
        c->exitHoverZone = EXIT_ZONE_EXIT;
        break;
    case 9:
        c->pickerOpen = 1;
        c->panelFades[FADE_PICKER].level = 1.0f;
        break;
    case 10:
        s->launcher.mode = LAUNCHER_PCS;
        s->launcher.pcs = s->pcs;
        s->launcher.pcCount = 6;
        break;
    case 11:
        s->launcher.mode = LAUNCHER_APPS;
        s->launcher.apps = s->apps;
        s->launcher.appCount = 7;
        s->launcher.pcName = "gaming-pc";
        break;
    case 12:
        c->reportOpen = 1;
        break;
    default: break;
    }
}

static const char* statsFor(int step) {
    static char buf[64];
    snprintf(buf, sizeof(buf), "Display 90 Hz\nstep %d", step);
    return buf;
}

static void sameState(Scene* a, Scene* b, int step) {
    XrCtx* ca = a->ctx;
    XrCtx* cb = b->ctx;
    int same = memcmp(xrAtlasPixels(ca), xrAtlasPixels(cb), atlasBytes()) == 0;
    if (!same) fprintf(stderr, "step %d: atlas differs\n", step);
    CHECK(same);
    const UiHits* ha = xrPanelsLauncherHits(ca);
    const UiHits* hb = xrPanelsLauncherHits(cb);
    CHECK(ha->count == hb->count);
    CHECK(memcmp(ha->hit, hb->hit, sizeof(UiHit) * (size_t)ha->count) == 0);
    CHECK(ca->reportReady == cb->reportReady);
    CHECK(ca->overlayHasContent == cb->overlayHasContent);
    CHECK(ca->toastArtReady == cb->toastArtReady);
    CHECK(ca->toastDrawnKind == cb->toastDrawnKind && ca->toastDrawnArg == cb->toastDrawnArg);
    CHECK(xrPanelsLastView(ca)->cog_tab == xrPanelsLastView(cb)->cog_tab);
    const Settings* sa = xrPanelsLastView(ca)->settings;
    const Settings* sb = xrPanelsLastView(cb)->settings;
    CHECK((sa == NULL) == (sb == NULL));
    if (sa != NULL && sb != NULL) CHECK(memcmp(sa, sb, sizeof(Settings)) == 0);
}

static double nowMs(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

int main(void) {
    Scene* a = scene_new();
    Scene* b = scene_new();
    CHECK(xrPanelsSetAsync(b->ctx));
    a->ctx->overlayVisible = b->ctx->overlayVisible = 1;

    // State by state, with a toast now and then
    for (int step = 0; step <= 12; step++) {
        setState(a, step);
        setState(b, step);
        if (step % 4 == 2) {
            noticePush(&a->ctx->notices, TOAST_3D_ON, 0);
            noticePush(&b->ctx->notices, TOAST_3D_ON, 0);
            Notice up;
            if (noticeAdvance(&a->ctx->notices, nowNs(), 0, &up)) xrPanelsToast(a->ctx, up.kind, up.arg);
            if (noticeAdvance(&b->ctx->notices, nowNs(), 0, &up)) xrPanelsToast(b->ctx, up.kind, up.arg);
        }
        scene_frame(a, statsFor(step));
        scene_frame(b, statsFor(step));
        xrPanelsFlush(b->ctx);
        sameState(a, b, step);
    }

    // A burst: the cog's tab and hover change every frame and nothing waits;
    // the frame loop's update stays far under a frame, and after a flush
    // the atlas holds the last state's drawing
    double worst = 0.0;
    for (int i = 0; i < 60; i++) {
        setState(b, 1 + i % 5);
        b->ctx->cogHoverSlider = i % 3;
        double t0 = nowMs();
        scene_frame(b, statsFor(100 + i));
        double d = nowMs() - t0;
        if (i > 0 && d > worst) worst = d;
    }
    xrPanelsFlush(b->ctx);
    // Same final state on the inline scene
    setState(a, 1 + 59 % 5);
    a->ctx->cogHoverSlider = 59 % 3;
    scene_frame(a, statsFor(159));
    sameState(a, b, 159);
    fprintf(stderr, "worker mode: worst update %.2f ms during the burst\n", worst);
    CHECK(worst < 3.0);

    // The report sheet reopened: no drawing of the last opening lands
    setState(b, 12);
    scene_frame(b, NULL);
    xrPanelsReportEvent(b->ctx, REPORT_OPENED, NULL);
    CHECK(b->ctx->reportReady == 0);
    xrPanelsFlush(b->ctx);
    scene_frame(b, NULL);
    xrPanelsFlush(b->ctx);
    CHECK(b->ctx->reportReady == 1);

    scene_free(a);
    scene_free(b);
    return checksDone("m4 panels async");
}
