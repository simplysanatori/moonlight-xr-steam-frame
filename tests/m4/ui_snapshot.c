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
#include "xr_host.h"
#include "xr_keyboard.h"

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

    // Native (lane wave3-hud): the clock strip over the cog with the headset's battery (charging) and the
    // slow connection badge
    scene_reset(s);
    c->cogOpen = 1;
    c->cogTab = COG_TAB_DISPLAY;
    c->cogPose = cogPanelPose(c, &c->cogW, &c->cogH);
    c->batteryPresent = 1;
    c->batteryPercent = 87;
    c->batteryCharging = 1;
    c->slowConn = 1;
    shot(s, "08b-cog-clock-battery", NULL);
    c->batteryPresent = 0;
    c->batteryCharging = 0;
    c->slowConn = 0;

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

    // Native (owner's request): the keyboard's move bar, lit under the ray,
    // and the keyboard carried off by it and laid at an angle
    c->kbState = KB_STATE_LOWER;
    c->kbHoverKey = -1;
    c->headPos = (XrVector3f){ 0, 0, 0 };
    {
        float bw, bh, drop;
        kbBarGeometry(c, &bw, &bh, &drop);
        Vec3 pill = panelPoint(c->kbPose, c->kbW, c->kbH, 0.5f, 0.5f + drop / c->kbH);
        scene_ray_to(s, hand(), pill);
        c->hoverKind = HOVER_KBBAR;
        shot(s, "10b-keyboard-bar", NULL);

        // Taken by the pill, brought up and in toward the viewer on a wrist
        // tipped back, the left a little
        Vec3 d = vecNorm(vecSub(pill, hand()));
        Vec3 z = { -d.x, -d.y, -d.z };
        Vec3 up = { 0, 1, 0 };
        Vec3 x = vecNorm(vecCross(up, z));
        Vec3 y = vecCross(z, x);
        XrPosef aim = { quatFromBasis(x, y, z), { hand().x, hand().y, hand().z } };
        xrGrabStepAt(c, aim, 1, 0, 0.0f);
        Vec3 xAxis = { 1, 0, 0 }, yAxis = { 0, 1, 0 };
        aim.orientation = quatNorm(quatMul(axisAngleQuat(yAxis, 0.25f),
                                           quatMul(axisAngleQuat(xAxis, 0.9f), aim.orientation)));
        aim.position.x -= 0.15f;
        aim.position.y += 0.10f;
        xrGrabStepAt(c, aim, 1, 0, 0.0f);
        for (int i = 0; i < 30; i++) xrGrabStepAt(c, aim, 1, 0, 0.6f);
        printf("moved keyboard at %.2f %.2f %.2f\n", c->kbPose.position.x, c->kbPose.position.y, c->kbPose.position.z);
        scene_ray_to(s, hand(), panelPoint(c->kbPose, c->kbW, c->kbH, 0.5f, 0.5f + drop / c->kbH));
        c->hoverKind = HOVER_KBBAR;
        shot(s, "10c-keyboard-moved", NULL);
        xrGrabStepAt(c, aim, 0, 0, 0.0f);
        c->hoverKind = HOVER_NONE;
        c->kbMoved = 0;
        kbPlace(c);
    }

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
    // Native (owner's request): the lines as the session writes them, the
    // app's clauses wider than the window was; every one wraps inside it
    static const char LONG_STATS[] =
        "Display 90 Hz, 89 fps\nVideo 2560x1440, 60 fps\ndecoded 123456, dropped 12, decode 3.1 ms\n"
        "units 654321, idr requests 2, rtt 4 ms, pending video 1\n"
        "hw decode 3.1 ms (last 3.2), frames 123456, skipped 12, errors 0\n"
        "audio 2ch 40 ms buffered, 2 underruns, 0 lost (stream totals)\n"
        "network 54 Mb/s, host encode 4.2 ms, network 1.1 ms, decode 3.1 ms, present 2.0 ms, total 10.4 ms (worst 15.8)\n"
        "Depth 14.9/s (target 15), depth-anything-v2-small-int8\nDepth inference 41.3 ms, age 88 ms, skipped 1234";
    shot(s, "13-stats", LONG_STATS);
    // and carried off by a grip: brought toward the viewer, to the right, turned
    {
        c->headPos = (XrVector3f){ 0, 0, 0 };
        statsPlace(c);
        Vec3 mid = panelPoint(c->statsPose, c->statsW, c->statsH, 0.5f, 0.5f);
        Vec3 d = vecNorm(vecSub(mid, hand()));
        Vec3 z = { -d.x, -d.y, -d.z };
        Vec3 up = { 0, 1, 0 };
        Vec3 x = vecNorm(vecCross(up, z));
        Vec3 y = vecCross(z, x);
        XrPosef aim = { quatFromBasis(x, y, z), { hand().x, hand().y, hand().z } };
        xrGrabStepAt(c, aim, 0, 1, 0.0f);
        Vec3 yAxis = { 0, 1, 0 };
        aim.orientation = quatNorm(quatMul(axisAngleQuat(yAxis, -0.5f), aim.orientation));
        aim.position.x += 0.9f;
        aim.position.y -= 0.35f;
        xrGrabStepAt(c, aim, 0, 1, 0.0f);
        for (int i = 0; i < 30; i++) xrGrabStepAt(c, aim, 0, 1, -0.5f);
        scene_ray_to(s, hand(), c->statsPose.position.x != 0.0f ? panelPoint(c->statsPose, c->statsW, c->statsH, 0.5f, 0.5f) : mid);
        shot(s, "13b-stats-moved", LONG_STATS);
        xrGrabStepAt(c, aim, 0, 0, 0.0f);
        c->statsMoved = 0;
    }
    // Native (lane wave3-hud): the HUD and Lite styles, as the session writes them from the stream's figures
    {
        MlxrHudFigures f;
        memset(&f, 0, sizeof(f));
        f.valid = 1;
        f.mbps = 54.2;
        f.hostMs = 4.2;
        f.recvMs = 1.1;
        f.queueMs = 0.4;
        f.decodeMs = 3.1;
        f.rttMs = 4.0;
        f.latencyMs = f.hostMs + f.rttMs * 0.5 + f.recvMs + f.queueMs + f.decodeMs;
        f.lossPct = 0.2;
        f.fps = 89.0;
        MlxrCompositorMetrics m;
        memset(&m, 0, sizeof(m));
        mlxr_metric_set(&m, "compositor/cpu_frametime", 1.4f);
        mlxr_metric_set(&m, "compositor/gpu_frametime", 3.2f);
        mlxr_metric_set(&m, "compositor/dropped_frame_count", 0.0f);
        char text[512];
        mlxr_hud_panel(&f, 0, &m, 90.0, text, sizeof(text));
        shot(s, "13c-stats-hud", text);
        mlxr_hud_lite(&f, 0, text, sizeof(text));
        shot(s, "13d-stats-lite", text);
    }
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
    const char* pcNames[3] = { "gaming-pc", "Living room PC", "Office laptop" };
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
    s->launcher.pcName = "gaming-pc";
    shot(s, "17-launcher-apps", NULL);
    c->exitConfirmOpen = 1;
    c->exitPose = exitPromptPose(c, &c->exitW, &c->exitH);
    shot(s, "18-exit-launcher", NULL);

    // Native (PLAN 4b.1): the Host sheet with two macros and a cell lit, then
    // asking before it sends Alt+F4
    scene_reset(s);
    xrMacrosParse(&c->macros, "[macros]\nMute all = key:ctrl+alt+m\nSnip = win+shift+s\n");
    c->hostOpen = 1;
    c->hostReady = 1;
    c->hostConfirm = -1;
    c->hostPose = hostSheetPose(c, &c->hostW, &c->hostH);
    c->hostHoverZone = 7;
    shot(s, "19-host", NULL);
    c->hostConfirm = 10;
    c->hostHoverZone = XR_HOST_ZONE_CONFIRM_SEND;
    shot(s, "20-host-confirm", NULL);
    // Native (PLAN 4.1): asking before it switches to PC VR
    c->hostConfirm = XR_HOST_CONFIRM_PCVR;
    c->hostHoverZone = XR_HOST_ZONE_CONFIRM_SEND;
    shot(s, "20b-host-pcvr-confirm", NULL);
    c->hostOpen = 0;
    c->hostConfirm = -1;
    c->hostHoverZone = XR_HOST_ZONE_NONE;
    xrMacrosClear(&c->macros);

    // Native (PLAN 3.11, 4b.1): the Full keyboard with its media row, waiting
    // for a combo with Ctrl and Alt lit
    scene_reset(s);
    c->kbOpen = 1;
    xrKeyboardSetLayout(c, NULL, KB_LAYOUT_FULL);
    c->kbState = KB_STATE_LOWER;
    c->kbMods = 0;
    kbPlace(c);
    xrHostChordStart(&c->hostChord);
    c->hostChord.mods = KBF_LCTRL | KBF_LALT;
    shot(s, "21-keyboard-full-chord", NULL);
    xrHostChordStop(&c->hostChord);
    xrKeyboardSetLayout(c, NULL, KB_LAYOUT_COMPACT);

    // Native (PLAN 4b.13): the Compact keyboard with Ctrl held down to lock
    scene_reset(s);
    c->kbOpen = 1;
    c->kbState = KB_STATE_FN;
    c->kbMods = KB_MOD_CTRL;
    c->kbSticky.locked = KB_MOD_CTRL;
    kbPlace(c);
    shot(s, "22-keyboard-locked", NULL);
    c->kbSticky.locked = 0;
    c->kbMods = 0;

    scene_free(s);
    if (g_over) {
        fprintf(stderr, "a state goes over 16 layers\n");
        return 1;
    }
    return 0;
}
