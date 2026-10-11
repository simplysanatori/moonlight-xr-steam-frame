// Moonlight XR Native, tests/m4: the budget. One UI atlas holds every panel,
// button face and piece of code drawn art without overlap; the session makes
// at most four swapchains (the picture, the atlas, passthrough's transparent
// projection layer, the curved picture's projection layer) of SteamVR's 16;
// and the busiest frames stay under its 16 layers. GPLv3.
#include "check.h"
#include "scene.h"

// The swapchains xr_session.c and xr_assets.c make, at most, at once
#define NATIVE_SWAPCHAINS_MAX 4

int main(void) {
    for (int a = 0; a < ATLAS_SLOTS; a++) {
        UiRectPx r = xrAtlasRect(a);
        CHECK(r.w > 0 && r.h > 0 && r.x >= 0 && r.y >= 0 && r.x + r.w <= UI_ATLAS_W && r.y + r.h <= UI_ATLAS_H);
        for (int b = a + 1; b < ATLAS_SLOTS; b++) {
            UiRectPx q = xrAtlasRect(b);
            int overlap = r.x < q.x + q.w && q.x < r.x + r.w && r.y < q.y + q.h && q.y < r.y + r.h;
            CHECK(!overlap);
            if (overlap) fprintf(stderr, "  slots %d and %d overlap\n", a, b);
        }
    }
    CHECK(NATIVE_SWAPCHAINS_MAX <= 16);

    // The busiest frames: the bar with every button, the stats, the toast and
    // the ray, head locked (head aim's button too); a settings tab with the
    // keyboard closed; the keyboard; each one plus passthrough's layer
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
    printf("bar, head locked, stats, toast, ray: %u layers (+1 passthrough)\n", s->out.count);
    CHECK(s->out.count + 1 <= 16);
    c->hoverKind = HOVER_NONE;
    c->cogOpen = 1;
    c->cogPose = cogPanelPose(c, &c->cogW, &c->cogH);
    scene_frame(s, "stats");
    printf("settings panel, stats, toast, ray: %u layers (+1 passthrough)\n", s->out.count);
    CHECK(s->out.count + 1 <= 16);
    c->cogOpen = 0;
    c->kbOpen = 1;
    c->kbPose = kbPanelPose(c, &c->kbW, &c->kbH);
    c->kbHoverKey = 3;
    scene_frame(s, "stats");
    printf("keyboard, stats, toast, ray: %u layers (+1 passthrough)\n", s->out.count);
    CHECK(s->out.count + 1 <= 16);
    // Native: the keyboard's move bar is a layer of its own, lit under the ray
    c->hoverKind = HOVER_KBBAR;
    scene_frame(s, "stats");
    printf("keyboard, its move bar lit, stats, toast, ray: %u layers (+1 passthrough)\n", s->out.count);
    CHECK(s->out.count + 1 <= 16);
    scene_free(s);
    return checksDone("m4 budget");
}
