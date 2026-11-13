// Moonlight XR Native, tests/m4: the layer gate, an off-headset perf gate on what a session
// submits to the compositor. A 144 Hz session (display period 6.94 ms, 42 s: head turns, the
// pointer over the picture, its bar, corners and halo, the keyboard's keys and the gaps between
// them, the settings panel, the room grid and the exit sheet opening and closing, gamepad mode
// with no beam) goes through the input code (xrPointerPanelStep, the seams as in test_replay)
// and xrBuildLayers, and every frame's layers are checked:
//   (a) no layer smaller than 1 cm across, or fully transparent (the degenerate stand-ins of
//       issue #5, which made SteamVR judder at 144 Hz)               always fatal
//   (b) no more layers than SteamVR takes (16)                        always fatal
//   (c) the beam's and the dot's index in the list does not change between consecutive frames
//       when no panel opened or closed (the beam streak, 2026-10-31)  warning; MLXR_GATE_STRICT=1
//   (d) the layer count does not change more than GATE_MAX_CHANGES_PER_S times a second
//       outside the panel events                                      warning; MLXR_GATE_STRICT=1
// 1.3.1 shifts the beam (hover-only layers come and go ahead of it, and the held places that
// kept it still are off by default), so (c) and (d) only warn there; the beam-streak fix turns
// the switch on. A per-second summary is printed.
// MLXR_GATE_CSV=file replays a real session instead: a ray recorder CSV (MLXR_RAY_DEBUG=1,
// reports/ray-*.csv), its poses and buttons through the same input code; the CSV does not
// say which panels were open, MLXR_GATE_KB=1 puts the keyboard up for the whole run.
// MLXR_GATE_WRITE=file writes the synthetic session out as such a CSV. GPLv3.
#include "check.h"
#include "scene.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The runtime's action sync has no session to run on here; the executable's own copy takes the call
// (the loader is static: the link allows the duplicate, see CMakeLists.txt), and the controllers are read through the seams as in test_replay
XrResult XRAPI_CALL xrSyncActions(XrSession session, const XrActionsSyncInfo* info) {
    (void)session; (void)info;
    return XR_SUCCESS;
}

#define FRAME_NS 6944444LL           // 144 Hz
#define SESSION_S 42
#define FPS 144
#define LAYER_CAP 16
#define GATE_MAX_CHANGES_PER_S 4     // a settled UI changes its count a few times a second at most
#define EVENT_WINDOW 12              // frames after a panel opened or closed (its fade) left out of (c), (d)
#define MIN_ACROSS 0.01f             // m

#define FAKE(n) ((uintptr_t)(n))
#define RIGHT_AIM ((XrSpace)FAKE(0x202))
#define LEFT_AIM ((XrSpace)FAKE(0x201))
#define VIEW ((XrSpace)FAKE(0x203))
#define LOCAL ((XrSpace)FAKE(0x204))

static RayLogRow cur;

static XrResult fakeLocate(XrCtx* ctx, XrSpace space, XrSpace base, XrTime time, XrSpaceLocation* out) {
    (void)ctx; (void)base; (void)time;
    if (space == VIEW) {
        out->locationFlags = cur.headFlags;
        out->pose = cur.head;
        return XR_SUCCESS;
    }
    int h = space == LEFT_AIM ? 0 : (space == RIGHT_AIM ? 1 : -1);
    if (h < 0 || !cur.hand[h].ok) {
        return XR_ERROR_RUNTIME_FAILURE;
    }
    out->locationFlags = cur.hand[h].flags;
    out->pose = cur.hand[h].pose;
    return XR_SUCCESS;
}
static int fakePoseActive(XrCtx* ctx, XrAction a, int hand) {
    (void)ctx; (void)a;
    return hand >= 0 && hand < 2 && cur.hand[hand].active;
}
static float fakeFloat(XrCtx* ctx, XrAction a, int hand) {
    if (hand < 0 || hand > 1) return 0.0f;
    return a == ctx->triggerAction ? cur.hand[hand].trigger : (a == ctx->grabAction ? cur.hand[hand].grip : 0.0f);
}
static int fakeBool(XrCtx* ctx, XrAction a, int hand) {
    if (hand < 0 || hand > 1) return 0;
    unsigned b = cur.hand[hand].buttons;
    return (a == ctx->rightClickAction && (b & RAYLOG_BTN_RIGHT_CLICK))
        || (a == ctx->middleClickAction && (b & RAYLOG_BTN_MIDDLE_CLICK))
        || (a == ctx->toggleAction && (b & RAYLOG_BTN_STICK_CLICK));
}
static XrVector2f fakeVec2(XrCtx* ctx, XrAction a, int hand) {
    XrVector2f z = { 0, 0 };
    return hand >= 0 && hand < 2 && a == ctx->scrollAction ? cur.hand[hand].stick : z;
}

static void setUp(Scene* s) {
    XrCtx* c = s->ctx;
    float place[10] = { 0.0f, 0.0f, -2.5f, 0.0f, 0.0f, 0.0f, 1.0f, 2.4f, 2.5f, -1.0f };
    xrSetScreenPose(c, place);
    c->viewSpace = VIEW;
    c->localSpace = LOCAL;
    c->aimSpaces[0] = LEFT_AIM;
    c->aimSpaces[1] = RIGHT_AIM;
    c->aimAction = (XrAction)FAKE(0x401);
    c->triggerAction = (XrAction)FAKE(0x402);
    c->grabAction = (XrAction)FAKE(0x403);
    c->scrollAction = (XrAction)FAKE(0x404);
    c->rightClickAction = (XrAction)FAKE(0x405);
    c->middleClickAction = (XrAction)FAKE(0x406);
    c->toggleAction = (XrAction)FAKE(0x407);
    c->jointTracking = 0;
    c->locateSpace = fakeLocate;
    c->poseActive = fakePoseActive;
    c->readFloat = fakeFloat;
    c->readBool = fakeBool;
    c->readVec2 = fakeVec2;
    c->pointerSleepOn = 0;
    c->padMode = 0;
    c->eyeGaze = 0;
    c->profileKind[0] = PROFILE_CONTROLLER;
    c->profileKind[1] = PROFILE_CONTROLLER;
    c->headPos = (XrVector3f){ 0, 0, 0 };
    c->inputReady = 1;
    c->placementValid = 1;
    c->pointerOn = 1;
    c->splash.phase = SPLASH_GONE;
}

// ---- the synthetic session ----
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
static float tri(float t) {   // 0..1..0, period 1
    t -= floorf(t);
    return t < 0.5f ? t * 2.0f : 2.0f - t * 2.0f;
}

enum { SEG_PICTURE, SEG_WIDE, SEG_VOID, SEG_STATS, SEG_KEYS, SEG_KB_OFF, SEG_COG, SEG_ROOMS, SEG_EXIT, SEG_PAD,
       SEG_HEADTURN, SEG_LOCKED, SEG_DONE };
typedef struct { int seg; float until; const char* name; } Seg;
static const Seg SEGS[] = {
    { SEG_PICTURE, 5, "pointer over the picture, head turning" },
    { SEG_WIDE, 10, "pointer over the bar, corners, halo and off the edge" },
    { SEG_VOID, 12, "pointer in the void" },
    { SEG_STATS, 14, "stats window on" },
    { SEG_KEYS, 22, "keyboard: keys, gaps, its bar" },
    { SEG_KB_OFF, 24, "keyboard closed, picture" },
    { SEG_COG, 28, "settings panel" },
    { SEG_ROOMS, 31, "room grid" },
    { SEG_EXIT, 33, "exit sheet" },
    { SEG_PAD, 36, "gamepad mode (no beam)" },
    { SEG_HEADTURN, 39, "big head turns, world locked" },
    { SEG_LOCKED, 42, "head locked" },
};

typedef struct { int cog, kb, picker, exit, stats, pad, locked; } Panels;

static void applyPanels(Scene* s, int seg, Panels* p) {
    XrCtx* c = s->ctx;
    memset(p, 0, sizeof(*p));
    p->kb = seg == SEG_KEYS;
    p->cog = seg == SEG_COG;
    p->picker = seg == SEG_ROOMS;
    p->exit = seg == SEG_EXIT;
    p->stats = seg == SEG_STATS;
    p->pad = seg == SEG_PAD;
    p->locked = seg == SEG_LOCKED;
    if (c->kbOpen != p->kb) { c->kbOpen = p->kb; c->kbMoved = 0; if (p->kb) kbPlace(c); }
    if (c->cogOpen != p->cog) { c->cogOpen = p->cog; c->cogTab = COG_TAB_ACCESS; if (p->cog) c->cogPose = cogPanelPose(c, &c->cogW, &c->cogH); }
    c->pickerOpen = p->picker;
    if (c->exitConfirmOpen != p->exit && p->exit) c->exitPose = exitPromptPose(c, &c->exitW, &c->exitH);
    c->exitConfirmOpen = p->exit;
    c->overlayVisible = p->stats;
    c->padMode = p->pad;
    s->settings.head_locked = p->locked;
    c->headLockedPref = p->locked;
}

static Vec3 targetFor(Scene* s, int seg, float t, float local) {
    XrCtx* c = s->ctx;
    float ph = local * 0.8f;
    switch (seg) {
    case SEG_PICTURE: case SEG_PAD: case SEG_KB_OFF: case SEG_HEADTURN: case SEG_LOCKED:
        return (Vec3){ -1.1f + 2.2f * tri(ph * 0.7f), -0.65f + 1.3f * tri(ph * 1.3f), -2.5f };
    case SEG_WIDE:   // across and below the picture, over the bar and out to the corners and past them
        return (Vec3){ -1.9f + 3.8f * tri(ph * 0.9f), -1.8f + 3.0f * tri(ph * 0.31f), -2.5f };
    case SEG_VOID:
        return (Vec3){ 3.0f * tri(ph), 2.5f, -2.0f };
    case SEG_STATS:
        return (Vec3){ -1.9f + 3.8f * tri(ph * 0.5f), -0.6f + 1.6f * tri(ph * 0.9f), -2.5f };
    case SEG_KEYS: { // raster over the keyboard with its bar and a margin
        float u = -0.05f + 1.1f * tri(local * 0.55f);
        float v = -0.1f + 1.3f * tri(local * 0.06f + 0.5f * tri(local * 0.31f));
        return onPlane(c->kbPose, c->kbW, c->kbH, u, v);
    }
    case SEG_COG:
        return onPlane(c->cogPose, c->cogW, c->cogH, -0.05f + 1.1f * tri(local * 0.4f), 0.05f + 0.9f * tri(local * 0.13f));
    case SEG_ROOMS: case SEG_EXIT:
    default:
        return (Vec3){ -1.5f + 3.0f * tri(ph * 0.6f), -1.0f + 1.5f * tri(ph * 0.35f), -2.5f };
    }
    (void)t;
}

static void synthRow(Scene* s, int seg, int frame, float local, RayLogRow* r) {
    float t = (float)frame / FPS;
    memset(r, 0, sizeof(*r));
    r->t = 1000000000LL + (int64_t)frame * FRAME_NS;
    r->toHead = seg == SEG_LOCKED;
    float yaw = seg == SEG_HEADTURN ? 1.1f * sinf(t * 1.7f) : 0.3f * sinf(t * 0.6f);
    float pitch = 0.15f * sinf(t * 0.9f);
    r->headFlags = 0xf;
    r->head.orientation = quatNorm(quatMul(axisAngleQuat((Vec3){ 0, 1, 0 }, yaw), axisAngleQuat((Vec3){ 1, 0, 0 }, pitch)));
    r->head.position = (XrVector3f){ 0, 0, 0 };
    Vec3 hand = { 0.25f, -0.35f, -0.3f };
    Vec3 target = targetFor(s, seg, t, local);
    r->hand[1].kind = PROFILE_CONTROLLER;
    r->hand[1].ok = 1;
    r->hand[1].flags = 0xf;
    r->hand[1].active = 1;
    r->hand[1].pose = aimAt(hand, target);
    r->hand[0].kind = PROFILE_CONTROLLER;   // in the lap, not tracked
    r->chosen = -9;
}

// ---- the checks ----
typedef struct {
    int total, seconds;
    int degenerate, overCap;
    int beamShifts, dotShifts, countChanges;
    int beamFrames, exempt;
    int minLayers, maxLayers;
    float smallest;
} Sec;

static int findSlot(const Scene* s, int slot) {
    UiRectPx b = xrAtlasRect(slot);
    for (uint32_t i = 0; i < s->out.count; i++) {
        const XrCompositionLayerQuad* q = (const XrCompositionLayerQuad*)s->out.order[i];
        if (q->type == XR_TYPE_COMPOSITION_LAYER_QUAD && q->subImage.imageRect.offset.x == b.x
                && q->subImage.imageRect.offset.y == b.y && q->subImage.imageRect.extent.width == b.w) {
            return (int)i;
        }
    }
    return -1;
}

static int transparent(Scene* s, const XrCompositionLayerQuad* q) {
    if (q->subImage.swapchain == SCENE_VIDEO_SWAPCHAIN) return 0;
    const uint8_t* px = xrAtlasPixels(s->ctx);
    const XrRect2Di* r = &q->subImage.imageRect;
    if (r->extent.width <= 0 || r->extent.height <= 0) return 1;
    if (!(q->layerFlags & XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT)) return 0;
    for (int y = r->offset.y; y < r->offset.y + r->extent.height; y++) {
        for (int x = r->offset.x; x < r->offset.x + r->extent.width; x++) {
            if (px[((size_t)y * UI_ATLAS_W + (size_t)x) * 4 + 3]) return 0;
        }
    }
    return 1;
}

int main(void) {
    const char* csvPath = getenv("MLXR_GATE_CSV");
    const char* writePath = getenv("MLXR_GATE_WRITE");
    int strict = getenv("MLXR_GATE_STRICT") != NULL && getenv("MLXR_GATE_STRICT")[0] == '1';
    setenv("XR_LOADER_DEBUG", "none", 1);   // the loader grumbles about the handles that are only seams here
    Scene* s = scene_new();
    XrCtx* c = s->ctx;
    setUp(s);
    FILE* csv = NULL;
    FILE* wr = NULL;
    if (csvPath != NULL && (csv = fopen(csvPath, "r")) == NULL) {
        fprintf(stderr, "layergate: cannot read %s\n", csvPath);
        return 1;
    }
    if (writePath != NULL && (wr = fopen(writePath, "w")) != NULL) {
        char h[2048];
        rayLogHeader(h, sizeof(h));
        fprintf(wr, "%s\n", h);
    }
    if (csv != NULL && getenv("MLXR_GATE_KB") != NULL) { c->kbOpen = 1; kbPlace(c); }

    int frames = csv ? 1 << 30 : SESSION_S * FPS;
    Sec sec = { 0 };
    sec.minLayers = 99;
    sec.smallest = 9.0f;
    int tot_degenerate = 0, tot_over = 0, tot_beam = 0, tot_dot = 0, tot_count = 0, tot_exempt = 0, tot_beamFrames = 0;
    int worstChanges = 0, secIdx = 0, frameInSec = 0;
    int prevBeam = -2, prevDot = -2, prevCount = -1, sinceEvent = 1000, segIdx = 0, lastSeg = -1;
    Panels prev;
    memset(&prev, 0, sizeof(prev));
    float segStart = 0.0f;
    char line[2048];
    float smallestAll = 9.0f;
    printf("layer gate: %s, strict %d, held places %s\n", csv ? csvPath : "synthetic 144 Hz session", strict,
           getenv("MLXR_HOLD_PLACES") ? getenv("MLXR_HOLD_PLACES") : "off");
    printf(" sec  frames  layers  beam#  beamShift dotShift countChg degenerate  smallest(cm)  what\n");

    for (int f = 0; f < frames; f++) {
        float t = (float)f / FPS;
        Panels p;
        int seg = -1;
        if (csv) {
            RayLogRow row;
            int got = 0;
            while (fgets(line, sizeof(line), csv) != NULL) {
                if (rayLogParse(line, &row)) { got = 1; break; }
            }
            if (!got) break;
            cur = row;
            memset(&p, 0, sizeof(p));
            p.kb = c->kbOpen;
            p.locked = row.toHead;
            s->settings.head_locked = row.toHead;
            c->headLockedPref = row.toHead;
            c->profileKind[0] = row.hand[0].kind;
            c->profileKind[1] = row.hand[1].kind;
        }
        else {
            while (segIdx < (int)(sizeof(SEGS) / sizeof(SEGS[0])) - 1 && t >= SEGS[segIdx].until) {
                segStart = SEGS[segIdx].until;
                segIdx++;
            }
            seg = SEGS[segIdx].seg;
            if (seg != lastSeg) {
                lastSeg = seg;
            }
            applyPanels(s, seg, &p);
            synthRow(s, seg, f, t - segStart, &cur);
        }
        c->predictedDisplayTime = (XrTime)cur.t;
        if (wr) {
            char buf[2048];
            if (rayLogFormat(&cur, buf, sizeof(buf))) fprintf(wr, "%s\n", buf);
        }
        float out[IN_SLOTS];
        float distance, quadWidth, curvature;
        xrDefaultPlacement(&distance, &quadWidth, &curvature);
        xrUpdateInput(c, distance, quadWidth, curvature, cur.toHead, 1, 0, 0, out);
        scene_frame(s, NULL);

        int panelEvent = memcmp(&p, &prev, sizeof(p)) != 0;
        prev = p;
        sinceEvent = panelEvent ? 0 : sinceEvent + 1;
        int exempt = sinceEvent < EVENT_WINDOW;

        // (a) and (b)
        unsigned n = s->out.count;
        for (uint32_t i = 0; i < n; i++) {
            const XrCompositionLayerQuad* q = (const XrCompositionLayerQuad*)s->out.order[i];
            if (q->type != XR_TYPE_COMPOSITION_LAYER_QUAD) continue;
            float across = q->size.width > q->size.height ? q->size.width : q->size.height;
            if (across < sec.smallest) sec.smallest = across;
            if (across < smallestAll) smallestAll = across;
            int bad = across < MIN_ACROSS || transparent(s, q);
            if (bad) {
                if (sec.degenerate == 0 && tot_degenerate < 5) {
                    fprintf(stderr, "  frame %d: layer %u of %u is %.1f mm x %.1f mm%s\n", f, i, n,
                            q->size.width * 1000, q->size.height * 1000, transparent(s, q) ? ", fully transparent" : "");
                }
                sec.degenerate++;
                tot_degenerate++;
            }
        }
        if (n > LAYER_CAP || n > (unsigned)c->maxLayerCount) {
            sec.overCap++;
            tot_over++;
        }
        // (c) and (d)
        int beam = findSlot(s, ATLAS_BEAM), dot = findSlot(s, ATLAS_DOT);
        if (prevCount >= 0 && !exempt) {
            if (beam >= 0 && prevBeam >= 0 && beam != prevBeam) { sec.beamShifts++; tot_beam++; }
            if (dot >= 0 && prevDot >= 0 && dot != prevDot) { sec.dotShifts++; tot_dot++; }
            if ((int)n != prevCount) { sec.countChanges++; tot_count++; }
        }
        if (exempt) { sec.exempt++; tot_exempt++; }
        if (beam >= 0) { sec.beamFrames++; tot_beamFrames++; }
        prevBeam = beam; prevDot = dot; prevCount = (int)n;
        if ((int)n < sec.minLayers) sec.minLayers = (int)n;
        if ((int)n > sec.maxLayers) sec.maxLayers = (int)n;
        sec.total++;
        if (++frameInSec == FPS) {
            printf("%4d  %5d  %2d-%2d  %5s  %9d %8d %8d %11d  %11.1f  %s\n", secIdx, sec.total, sec.minLayers, sec.maxLayers,
                   sec.beamFrames ? "yes" : "no", sec.beamShifts, sec.dotShifts, sec.countChanges, sec.degenerate,
                   sec.smallest * 100, csv ? "" : SEGS[segIdx].name);
            if (sec.countChanges > worstChanges) worstChanges = sec.countChanges;
            secIdx++;
            frameInSec = 0;
            memset(&sec, 0, sizeof(sec));
            sec.minLayers = 99;
            sec.smallest = 9.0f;
        }
    }
    if (frameInSec > 0) {
        if (sec.countChanges > worstChanges) worstChanges = sec.countChanges;
    }
    if (csv) fclose(csv);
    if (wr) fclose(wr);

    printf("totals: %d frames, %d not counted for (c)/(d) (panel events), beam on in %d\n", secIdx * FPS + frameInSec, tot_exempt, tot_beamFrames);
    printf("(a) degenerate layers submitted:       %d  (smallest layer %.1f mm)\n", tot_degenerate, smallestAll * 1000);
    printf("(b) frames over %d layers:              %d\n", LAYER_CAP, tot_over);
    printf("(c) frames the beam's index shifted:   %d (the dot's: %d)\n", tot_beam, tot_dot);
    printf("(d) layer count changes, worst second: %d (limit %d, %d in all)\n", worstChanges, GATE_MAX_CHANGES_PER_S, tot_count);
    CHECK(tot_degenerate == 0);
    CHECK(tot_over == 0);
    int soft = (tot_beam > 0 || tot_dot > 0) + (worstChanges > GATE_MAX_CHANGES_PER_S);
    if (soft) {
        printf("%s: the beam/dot index moves between frames with no panel opening or closing, or the layer count churns.\n"
               "  Hover-only layers come and go ahead of the beam. Expected on 1.3.1 until the beam-streak fix lands;\n"
               "  MLXR_GATE_STRICT=1 makes it fatal.\n", strict ? "FAIL" : "WARNING (expected on 1.3.1)");
        if (strict) {
            CHECK(tot_beam == 0 && tot_dot == 0);
            CHECK(worstChanges <= GATE_MAX_CHANGES_PER_S);
        }
    }
    scene_free(s);
    return checksDone("m4_layer_gate");
}
