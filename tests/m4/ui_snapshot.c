// Moonlight XR Native, tests/m4: mlxr-ui-snapshot DIR. Puts the in-headset UI
// in each of its states without a headset (a CPU UI atlas and the ported
// xr_ui/xr_input/xr_layers code), builds the frame's layers as the session
// would, and composes them from one eye into DIR/<state>.png, with the layer
// count of each. Exits 1 if a state would go over SteamVR's 16 layers.
// GPLv3.
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "scene.h"

static const char* g_dir;
static int g_over;

static void shot(Scene* s, const char* name, const char* stats) {
    xrPanelsStats(s->ctx, &(int){ 0 }, &(double){ 0 });
    scene_frame(s, stats);
    int draws = 0;
    double ms = 0.0;
    xrPanelsStats(s->ctx, &draws, &ms);
    char path[600];
    snprintf(path, sizeof(path), "%s/%s.png", g_dir, name);
    int quads = scene_render(s, 1600, 1200, 84.0f, path);
    printf("%-22s %2u layers (%d quads), %d panel drawings %.1f ms avg -> %s\n", name, s->out.count, quads, draws,
           ms, path);
    if (s->out.count > 16) g_over = 1;
}

static Vec3 hand(void) {
    Vec3 h = { 0.25f, -0.35f, -0.3f };
    return h;
}

// Where a bar slot's button sits in the space
static Vec3 barButtonPoint(XrCtx* ctx, int slot) {
    BarFrame f = barFrame(ctx);
    Vec3 local;
    float side;
    barSlotPlacement(slot, f.width, f.height, &local, &side);
    Vec3 off = quatRotate(f.pose.orientation, local);
    Vec3 p = { f.pose.position.x + off.x, f.pose.position.y + off.y, f.pose.position.z + off.z };
    return p;
}

// A point of a panel at (u, v) of it
static Vec3 panelPoint(XrPosef pose, float w, float h, float u, float v) {
    Vec3 local = { (u - 0.5f) * w, (0.5f - v) * h, 0.0f };
    Vec3 off = quatRotate(pose.orientation, local);
    Vec3 p = { pose.position.x + off.x, pose.position.y + off.y, pose.position.z + off.z };
    return p;
}

int main(int argc, char** argv) {
    g_dir = argc > 1 ? argv[1] : "ui-snapshots";
    mkdir(g_dir, 0755);
    Scene* s = scene_new();
    XrCtx* c = s->ctx;

    shot(s, "01-picture", NULL);

    c->hoverKind = HOVER_BAR;
    BarFrame f = barFrame(c);
    Vec3 pill = { f.pose.position.x, f.pose.position.y - (f.height * 0.5f + f.width * BAR_DROP_FRAC), f.pose.position.z };
    scene_ray_to(s, hand(), pill);
    shot(s, "02-bar", NULL);

    c->hoverKind = HOVER_COGBUTTON;
    c->cogButtonHot = 1;
    scene_ray_to(s, hand(), barButtonPoint(c, BAR_SLOT_COG));
    shot(s, "03-bar-cog-hover", NULL);
    scene_reset(s);

    static const char* const TABS[COG_TAB_COUNT] = { "screen", "display", "3d", "picture", "about" };
    for (int tab = 0; tab < COG_TAB_COUNT; tab++) {
        scene_reset(s);
        c->cogOpen = 1;
        c->cogTab = tab;
        c->cogPose = cogPanelPose(c, &c->cogW, &c->cogH);
        if (tab == COG_TAB_DISPLAY) {
            c->cogHoverSlider = COG_OPTION_RAY;
            c->cogHoverCell = 1;
        }
        else if (tab == COG_TAB_SCREEN) {
            c->cogHoverSlider = COG_SLIDER_SIZE;
            c->cogHoverStep = 1;
        }
        else if (tab == COG_TAB_PICTURE) {
            c->cogHoverSlider = PICTURE_CONTRAST;
        }
        scene_ray_to(s, hand(), panelPoint(c->cogPose, c->cogW, c->cogH, 0.7f, 0.55f));
        char name[64];
        snprintf(name, sizeof(name), "%02d-cog-%s", 4 + tab, TABS[tab]);
        shot(s, name, NULL);
    }

    scene_reset(s);
    c->kbOpen = 1;
    c->kbState = KB_STATE_LOWER;
    c->kbPose = kbPanelPose(c, &c->kbW, &c->kbH);
    c->kbHoverKey = 12;
    const float* r = &c->kbKeyRects[12 * 4];
    scene_ray_to(s, hand(), panelPoint(c->kbPose, c->kbW, c->kbH, (r[0] + r[2]) * 0.5f, (r[1] + r[3]) * 0.5f));
    shot(s, "09-keyboard", NULL);
    c->kbState = KB_STATE_FN;
    c->kbMods = KB_MOD_CTRL;
    shot(s, "10-keyboard-fn", NULL);
    c->kbMods = 0;

    scene_reset(s);
    c->exitConfirmOpen = 1;
    c->exitPose = exitPromptPose(c, &c->exitW, &c->exitH);
    c->exitHoverZone = EXIT_ZONE_EXIT;
    scene_ray_to(s, hand(), panelPoint(c->exitPose, c->exitW, c->exitH, 0.3f, 0.75f));
    shot(s, "11-exit-stream", NULL);

    scene_reset(s);
    c->pickerOpen = 1;
    c->pickerHover = 0;
    float pw, ph;
    XrPosef pp = pickerPose(c, &pw, &ph);
    scene_ray_to(s, hand(), panelPoint(pp, pw, ph, 0.1f, 0.5f));
    shot(s, "12-picker", NULL);

    scene_reset(s);
    c->overlayVisible = 1;
    shot(s, "13-stats", "Display 72 Hz, 72 fps\nVideo 2560x1440, 60 fps\ndecoded 0, dropped 0\nhw decode 3.8 ms\nrtt 2 ms\naudio 2ch 15 ms buffered");
    c->overlayVisible = 0;

    scene_reset(s);
    noticePush(&c->notices, TOAST_HEAD_AIM_ON, 0);
    Notice up;
    if (noticeAdvance(&c->notices, nowNs(), 0, &up)) {
        xrPanelsToast(c, up.kind, up.arg);
    }
    shot(s, "14-toast", NULL);
    noticeInit(&c->notices);
    c->toastFade.level = 0.0f;

    scene_reset(s);
    c->hoverKind = HOVER_CORNER;
    c->hoverCorner = 3;
    shot(s, "15-corner", NULL);

    // The launcher: the lists in the picture's place
    scene_reset(s);
    c->videoShown = 0;
    const char* pcNames[3] = { "DESKTOP-EXAMPLE", "Living room PC", "Office laptop" };
    for (int i = 0; i < 3; i++) {
        memset(&s->pcs[i], 0, sizeof(s->pcs[i]));
        snprintf(s->pcs[i].name, sizeof(s->pcs[i].name), "%s", pcNames[i]);
        s->pcs[i].state = i == 0 ? UI_PC_ONLINE : (i == 1 ? UI_PC_OFFLINE : UI_PC_REFRESHING);
        s->pcs[i].paired = i != 2;
        s->pcs[i].game_running = i == 0;
    }
    s->launcher.mode = LAUNCHER_PCS;
    s->launcher.pcs = s->pcs;
    s->launcher.pcCount = 3;
    scene_frame(s, NULL); // the list's hits, for the ray to land on
    shot(s, "16-launcher-pcs", NULL);
    const char* appNames[7] = { "Desktop", "Steam Big Picture", "Cyberpunk 2077", "Half-Life: Alyx",
                                "Elden Ring", "Portal 2", "Notepad" };
    for (int i = 0; i < 7; i++) {
        memset(&s->apps[i], 0, sizeof(s->apps[i]));
        snprintf(s->apps[i].name, sizeof(s->apps[i].name), "%s", appNames[i]);
        s->apps[i].running = i == 0;
    }
    s->launcher.mode = LAUNCHER_APPS;
    s->launcher.apps = s->apps;
    s->launcher.appCount = 7;
    s->launcher.pcName = "DESKTOP-EXAMPLE";
    shot(s, "17-launcher-apps", NULL);
    c->exitConfirmOpen = 1;
    c->exitPose = exitPromptPose(c, &c->exitW, &c->exitH);
    shot(s, "18-exit-launcher", NULL);

    scene_free(s);
    if (g_over) {
        fprintf(stderr, "a state goes over 16 layers\n");
        return 1;
    }
    return 0;
}
