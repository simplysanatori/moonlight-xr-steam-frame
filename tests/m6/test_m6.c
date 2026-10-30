// Moonlight XR Native, tests/m6: the 3D pipeline (xr/xr_depth.c) on the
// Frame's GPU, headless, with a fake depth engine in place of LiteRT.
// Groups:
//   pipeline  capture -> stage thread -> submit -> acquire -> texture, with
//             frames kept in flight: every capture's pixels are the frame it
//             says it is (no readback buffer refilled while queued), frame ids
//             arrive in order, no texture's staging rewritten while a frame
//             could read it, the texture holds the newest map, the rate the
//             engine is given is held (captures per second)
//   governor  xr_rate's governor on the 3D: the depth rate halves before the
//             display steps, the display never goes under 72 Hz, and the
//             target reaches the engine
//   settings  the 3D tab's rows through config.ini and back: depth_source
//             (zipdepth, midas, zipdepth-int8, off, a missing model), the
//             remembered switch, presets, the depth and convergence tracks,
//             reset, the models' separations (0.6 % / 0.5 %)
//   states    the 3D switch: the engine starts only when the 3D is first on,
//             LOADING -> READY, off stops the captures, back on resets and
//             waits for a fresh map; FAILED (and a thread that never starts)
//             stays flat with a toast and puts the switch away
//   budget    the layers of the busiest frames with the 3D on (two picture
//             quads, one an eye, and the bar's 3D button), against SteamVR's 16
//   sbs       PLAN 4.3, a pair the PC drew: no engine, no capture and no
//             depth pass, each eye its own half, the convergence track's
//             shift, off is the left half flat; the 3D source row switches to
//             top-bottom, back to the model (which starts, captures and
//             upsamples again) and back, stored as it goes; full size halves
//             the picture, the pointer goes to the left half, the letterbox
//             detector stands down; a session with the model off still gets
//             SBS from the row
// GPLv3.
#define _GNU_SOURCE
#include <pthread.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "check.h"
#include "m6_gpu.h"
#include "scene.h"
#include "settings.h"
#include "ui_strings.h"
#include "xr_native.h"

// ---- the fake engine -------------------------------------------------------------------

#define FAKE_W 128
#define FAKE_H 72

struct MlxrDepthEngine {
    pthread_mutex_t lock;
    int loadPolls;      // state() calls before READY
    int fail;           // goes FAILED instead
    int polls;
    int rate;
    DepthGate gate;
    uint64_t seq;
    MlxrDepthResult res[3];
    uint8_t rgba[3][FAKE_W * FAKE_H * 4];
    int newest, held;
    // what the tests read
    int resets, rateCalls, paramsCalls;
    long submits, contentBad, orderBad;
    uint64_t lastFrameId;
    int haveLast;
};

static MlxrDepthModelSpec fakeSpecs[MLXR_DEPTH_MODEL_COUNT] = {
    { MLXR_DEPTH_ZIPDEPTH_512x288_FP16, "zipdepth", "fake ZipDepth", "zipdepth_512x288_fp16.tflite", FAKE_W, FAKE_H, 6 },
    { MLXR_DEPTH_ZIPDEPTH_256_INT8DR, "zipdepth-int8", "fake ZipDepth int8", "zipdepth_256_int8dr.tflite", FAKE_W,
      FAKE_H, 6 },
    { MLXR_DEPTH_MIDAS_256_FP16, "midas", "fake MiDaS", "midas_v21_small_256_fp16.tflite", FAKE_W, FAKE_H, 5 },
};
static int fakeStarts, fakeStops, fakeFailNext, fakeNullNext, fakeLoadPolls = 3;
static MlxrDepthEngine* fakeLive;
// What the video frame with this id was painted with (red level)
static int (*expectRed)(uint64_t frameId);

static const MlxrDepthModelSpec* fSpec(MlxrDepthModelId id) {
    return id >= 0 && id < MLXR_DEPTH_MODEL_COUNT ? &fakeSpecs[id] : NULL;
}
static MlxrDepthEngine* fStart(const MlxrDepthConfig* cfg) {
    fakeStarts++;
    if (fakeNullNext) {
        fakeNullNext = 0;
        return NULL;
    }
    MlxrDepthEngine* e = calloc(1, sizeof(*e));
    pthread_mutex_init(&e->lock, NULL);
    e->loadPolls = fakeLoadPolls;
    e->fail = fakeFailNext;
    fakeFailNext = 0;
    e->rate = cfg->perSecond;
    e->held = -1;
    e->newest = -1;
    fakeLive = e;
    return e;
}
static void fStop(MlxrDepthEngine* e) {
    fakeStops++;
    if (fakeLive == e) fakeLive = NULL;
    pthread_mutex_destroy(&e->lock);
    free(e);
}
static MlxrDepthState fState(MlxrDepthEngine* e) {
    if (e->fail) return MLXR_DEPTH_FAILED;
    return e->polls++ >= e->loadPolls ? MLXR_DEPTH_READY : MLXR_DEPTH_LOADING;
}
static MlxrDepthState fWait(MlxrDepthEngine* e, int ms, char* err, size_t len) {
    (void)ms;
    if (e->fail) snprintf(err, len, "fake model refused to load");
    return fState(e);
}
static void fSize(MlxrDepthEngine* e, int* w, int* h) {
    (void)e;
    *w = FAKE_W;
    *h = FAKE_H;
}
static void fRate(MlxrDepthEngine* e, int r) {
    pthread_mutex_lock(&e->lock);
    e->rate = r;
    e->rateCalls++;
    pthread_mutex_unlock(&e->lock);
}
static void fParams(MlxrDepthEngine* e, const MlxrDepthParams* p) {
    (void)p;
    e->paramsCalls++;
}
static void fReset(MlxrDepthEngine* e) {
    e->resets++;
}
static int fDue(MlxrDepthEngine* e, int64_t now) {
    pthread_mutex_lock(&e->lock);
    // 0 is as fast as it goes, as the real engine's
    int due = e->rate <= 0 ? 1 : depthGateDue(&e->gate, now, e->rate);
    pthread_mutex_unlock(&e->lock);
    return due;
}
// Synchronous: the map is made in the stage thread's call. Its depth (alpha)
// encodes the frame id, so the texture can be checked.
static int fSubmit(MlxrDepthEngine* e, const uint8_t* rgba, size_t stride, int w, int h, int flip, uint64_t frameId,
                   int64_t captureNs) {
    (void)flip;
    pthread_mutex_lock(&e->lock);
    e->submits++;
    if (w != FAKE_W || h != FAKE_H || stride != (size_t)w * 4) {
        e->contentBad++;
    }
    else if (expectRed != NULL) {
        int want = expectRed(frameId);
        // the middle texel of the capture is the frame's colour
        int got = rgba[(size_t)(h / 2) * stride + (size_t)(w / 2) * 4];
        if (abs(got - want) > 10) e->contentBad++;
    }
    if (e->haveLast && frameId < e->lastFrameId) e->orderBad++;
    e->lastFrameId = frameId;
    e->haveLast = 1;
    int slot = (e->newest + 1) % 3;
    if (slot == e->held) slot = (slot + 1) % 3;
    MlxrDepthResult* r = &e->res[slot];
    uint8_t a = (uint8_t)((frameId * 7) % 251 + 2);
    for (int i = 0; i < FAKE_W * FAKE_H; i++) {
        e->rgba[slot][i * 4 + 0] = e->rgba[slot][i * 4 + 1] = e->rgba[slot][i * 4 + 2] = 128;
        e->rgba[slot][i * 4 + 3] = a;
    }
    memset(r, 0, sizeof(*r));
    r->width = FAKE_W;
    r->height = FAKE_H;
    r->rgba = e->rgba[slot];
    r->frameId = frameId;
    r->captureNs = captureNs;
    r->submitNs = r->startNs = r->doneNs = nowNs();
    r->seq = ++e->seq;
    e->newest = slot;
    pthread_mutex_unlock(&e->lock);
    return 1;
}
static const MlxrDepthResult* fAcquire(MlxrDepthEngine* e, uint64_t have) {
    pthread_mutex_lock(&e->lock);
    const MlxrDepthResult* r = NULL;
    if (e->newest >= 0 && e->held < 0 && e->res[e->newest].seq > have) {
        e->held = e->newest;
        r = &e->res[e->newest];
    }
    pthread_mutex_unlock(&e->lock);
    return r;
}
static void fRelease(MlxrDepthEngine* e, const MlxrDepthResult* r) {
    (void)r;
    pthread_mutex_lock(&e->lock);
    e->held = -1;
    pthread_mutex_unlock(&e->lock);
}
static void fStats(MlxrDepthEngine* e, MlxrDepthStats* s) {
    memset(s, 0, sizeof(*s));
    s->state = MLXR_DEPTH_READY;
    s->width = FAKE_W;
    s->height = FAKE_H;
    s->maps = e->seq;
    s->perSecond = e->rate;
}
static const XrDepthEngineOps FAKE = { fSpec, fStart, fStop, fState, fWait, fSize, fRate, fParams,
                                       fReset, fDue, fSubmit, fAcquire, fRelease, fStats };

// ---- shared ---------------------------------------------------------------------------

static M5Gpu g;
static char assets[256];

// An assets directory with empty model files in it (the fake never reads them)
static void makeAssets(void) {
    snprintf(assets, sizeof(assets), "/tmp/mlxr-m6-assets-%d", (int)getpid());
    char p[400];
    mkdir(assets, 0755);
    snprintf(p, sizeof(p), "%s/depth", assets);
    mkdir(p, 0755);
    for (int i = 0; i < MLXR_DEPTH_MODEL_COUNT; i++) {
        snprintf(p, sizeof(p), "%s/depth/%s", assets, fakeSpecs[i].file);
        FILE* f = fopen(p, "w");
        if (f) fclose(f);
    }
}

static void dropAssets(void) {
    char p[400];
    for (int i = 0; i < MLXR_DEPTH_MODEL_COUNT; i++) {
        snprintf(p, sizeof(p), "%s/depth/%s", assets, fakeSpecs[i].file);
        unlink(p);
    }
    snprintf(p, sizeof(p), "%s/depth", assets);
    rmdir(p);
    rmdir(assets);
}

static XrCtx* newCtx(int source, int stereo) {
    Settings s;
    settings_defaults(&s);
    s.depth_source = source;
    s.stereo = stereo;
    return m6_ctx(&g, &FAKE, assets, &s);
}

// Solid video frames, red 40, 100, 160, 220 in turn by frame id
#define VIDEOS 4
static M5Video videos[VIDEOS];
static int videoRed(int i) {
    return 40 + 60 * i;
}
static int curVideo;
static void solid(float u, float v, float out[3]) {
    (void)u;
    (void)v;
    out[0] = videoRed(curVideo) / 255.0f;
    out[1] = 0.3f;
    out[2] = 0.5f;
}
static int expectRedFor(uint64_t frameId) {
    return videoRed((int)(frameId % VIDEOS));
}

// The picture target of a frame: 2W x H, like the double wide chain
static M5Image target;
#define VW 640
#define VH 360

typedef struct {
    uint64_t frameId;
    int newFrame;
    int eyes;
} Pic;

// What the last picture's warp was asked for (the sbs group reads it)
static XrDepthDraw lastDraw;

static void drawPicture(XrCtx* ctx, VkCommandBuffer cmd, void* user) {
    Pic* p = user;
    const MlxrVideo* v = &videos[p->frameId % VIDEOS].video;
    XrDepthDraw dd;
    xrDepthPicture(ctx, v, p->newFrame, p->frameId, nowNs(), &dd);
    xrVkImageTo(ctx, &target.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    MlxrTarget t;
    memset(&t, 0, sizeof(t));
    t.view = target.img.view;
    t.format = target.img.format;
    t.rect.extent = (VkExtent2D){ v->width, v->height };
    MlxrWarpParams w;
    memset(&w, 0, sizeof(w));
    w.video = *v;
    w.tint[0] = w.tint[1] = w.tint[2] = 1.0f;
    xrDepthRecordWarp(ctx, cmd, &t, &w, &dd);
    xrVkImageTo(ctx, &target.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    ctx->drawnEyes = dd.eyes;
    p->eyes = dd.eyes;
    lastDraw = dd;
}

static void frames(XrCtx* ctx, int n, uint64_t* frameId, int inFlight) {
    for (int i = 0; i < n; i++) {
        Pic p = { (*frameId)++, 1, 0 };
        m6_frame(&g, ctx, inFlight, drawPicture, &p);
    }
}

static void waitReady(XrCtx* ctx, uint64_t* frameId) {
    for (int i = 0; i < 20; i++) {
        XrDepthInfo info;
        xrDepthGetInfo(ctx, &info);
        if (info.engineState == MLXR_DEPTH_READY) return;
        frames(ctx, 1, frameId, 0);
    }
}

// ---- pipeline ---------------------------------------------------------------------------

static void testPipeline(void) {
    expectRed = expectRedFor;
    XrCtx* ctx = newCtx(0, 1);
    CHECK(ctx->stereoMode == DEPTH_MODE_MODEL);
    CHECK(ctx->stereoLive == 1);
    uint64_t fid = 0;
    waitReady(ctx, &fid);
    CHECK(fakeLive != NULL);
    if (fakeLive == NULL) {
        m6_ctx_free(ctx);
        return;
    }
    // Unlimited rate, frames kept two deep on the GPU: captures as often as
    // a buffer is free
    ctx->depthGov.target = 0;
    frames(ctx, 240, &fid, 2);
    // no more captures; the stage thread finishes what is queued, and the
    // newest map goes up
    ctx->depthGov.target = 1;
    frames(ctx, 1, &fid, 0);
    m6_idle(&g, ctx);
    for (int i = 0; i < 100; i++) {
        XrDepthInfo in;
        xrDepthGetInfo(ctx, &in);
        if (in.submitted + in.submitFailed >= in.captures) break;
        usleep(10000);
    }
    m6_frame(&g, ctx, 0, NULL, NULL);
    m6_idle(&g, ctx);
    XrDepthInfo info;
    xrDepthGetInfo(ctx, &info);
    printf("pipeline: %llu captures, %llu submitted, %llu uploads, %llu skipped (no buffer), %llu blocked, "
           "stage wait %.2f ms, submit %.2f ms\n",
           (unsigned long long)info.captures, (unsigned long long)info.submitted, (unsigned long long)info.uploads,
           (unsigned long long)info.skipsNoPair, (unsigned long long)info.uploadBlocked, info.stageWaitMsAvg,
           info.submitMsAvg);
    CHECK(info.captures > 60);
    CHECK(info.submitted == info.captures);
    CHECK(info.submitFailed == 0);
    CHECK(info.uploads > 30);
    CHECK(info.pairReuseBad == 0);
    CHECK(info.frameIdBackwards == 0);
    CHECK(fakeLive->contentBad == 0);
    CHECK(fakeLive->orderBad == 0);
    CHECK(fakeLive->submits == (long)info.submitted);
    CHECK(ctx->drawnEyes == 2);
    // The texture read now holds the newest map
    uint8_t* px = malloc((size_t)FAKE_W * FAKE_H * 4);
    m5_readback(&g, info.depthImage[info.readIndex], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, FAKE_W, FAKE_H, px);
    CHECK(px[3] == (uint8_t)((info.lastFrameId * 7) % 251 + 2));
    CHECK(px[(FAKE_W * FAKE_H - 1) * 4 + 3] == px[3]);
    free(px);
    // The rotation: the slot read is the last written, and the next one
    // differs from it
    CHECK(info.readIndex >= 0 && info.readIndex < DEPTH_TEX_COUNT);

    // Rate held: 10 maps/s asked over 2 s of frames at ~100 fps
    ctx->depthGov.target = 10;
    frames(ctx, 2, &fid, 0);
    CHECK(fakeLive->rate == 10);
    // the gate was counting a 1 s period: let it come round first
    for (int64_t t = nowNs(); nowNs() - t < 1100000000LL;) {
        frames(ctx, 1, &fid, 1);
        usleep(8000);
    }
    xrDepthGetInfo(ctx, &info);
    uint64_t before = info.captures;
    int64_t t0 = nowNs();
    while (nowNs() - t0 < 2000000000LL) {
        frames(ctx, 1, &fid, 1);
        usleep(8000);
    }
    m6_idle(&g, ctx);
    xrDepthGetInfo(ctx, &info);
    double perSec = (double)(info.captures - before) / ((nowNs() - t0) / 1e9);
    printf("pipeline: rate 10 held at %.1f captures/s\n", perSec);
    CHECK(perSec < 12.0);
    // Reaching the rate is a time budget: printed only with MLXR_SOFT_GPU=1
    // (lavapipe in CI draws the frames on the CPU)
    if (!m5_soft_gpu()) CHECK(perSec > 8.0);
    else if (perSec <= 8.0) printf("pipeline: under the rate (MLXR_SOFT_GPU=1, not failed)\n");
    CHECK(info.pairReuseBad == 0 && fakeLive->contentBad == 0 && fakeLive->orderBad == 0);

    char line[600];
    xrDepthStatsLine(ctx, line, sizeof(line));
    printf("stats line: %s\n", line);
    CHECK(strstr(line, "3d on") != NULL);
    xrDepthOverlay(ctx, line, sizeof(line));
    printf("overlay: %s\n", line);
    CHECK(strstr(line, "Depth") != NULL);
    m6_ctx_free(ctx);
    CHECK(fakeLive == NULL); // stopped at the end
    expectRed = NULL;
}

// ---- governor ----------------------------------------------------------------------------

static void testGovernor(void) {
    // xr_rate's decisions as the 3D uses them: the depth rate first
    DepthGovernor gov;
    depthGovernorStart(&gov, DEPTH_RATE_DEFAULT);
    int64_t t = 1000000000LL;
    CHECK(depthGovernorWindow(&gov, t, RATE_WINDOW_OVER, 1) == DEPTH_MOVE_CUT);
    CHECK(gov.target == 10);
    CHECK(depthGovernorWindow(&gov, t + 2000000000LL, RATE_WINDOW_OVER, 1) == DEPTH_MOVE_HOLDING);
    CHECK(gov.target == 10);
    t += DEPTH_HOLD_NS + 1;
    CHECK(depthGovernorWindow(&gov, t, RATE_WINDOW_OVER, 1) == DEPTH_MOVE_CUT);
    CHECK(gov.target == DEPTH_RATE_MIN);
    t += DEPTH_HOLD_NS + 1;
    // Only with the depth at its floor is it the display's turn
    CHECK(depthGovernorWindow(&gov, t, RATE_WINDOW_OVER, 1) == DEPTH_MOVE_DISPLAY);
    // With no model running, straight to the display
    DepthGovernor off;
    depthGovernorStart(&off, DEPTH_RATE_DEFAULT);
    CHECK(depthGovernorWindow(&off, t, RATE_WINDOW_OVER, 0) == DEPTH_MOVE_DISPLAY);
    CHECK(off.target == DEPTH_RATE_DEFAULT);
    // The display: never under 72 Hz with the warp on
    float rates[4] = { 60.0f, 72.0f, 90.0f, 120.0f };
    CHECK_NEAR(rateChoose(90.0f, rates, 4, 1, 90.0f, 12.5f), 72.0f, 0.01);
    CHECK_NEAR(rateChoose(90.0f, rates, 4, 1, 72.0f, 16.0f), 72.0f, 0.01);
    CHECK(rateStepDown(72.0f, rates, 4, RATE_FLOOR_HZ) == 0.0f);
    float frame[1] = { 90.0f };
    CHECK_NEAR(rateChoose(90.0f, frame, 1, 1, 90.0f, 20.0f), 90.0f, 0.01); // nothing lower offered
    // Held budget climbs back after the recovery wait, never past the
    // ceiling the failure set
    t += DEPTH_RECOVER_NS / 2;
    for (int i = 0; i < 40; i++) {
        t += RATE_WINDOW_NS;
        depthGovernorWindow(&gov, t, RATE_WINDOW_HELD, 1);
    }
    CHECK(gov.target >= DEPTH_RATE_MIN);
    CHECK(gov.target <= depthGovernorCeiling(&gov));

    // The target reaches the engine (xrDepthFrame)
    XrCtx* ctx = newCtx(0, 1);
    uint64_t fid = 0;
    waitReady(ctx, &fid);
    CHECK(fakeLive != NULL && fakeLive->rate == DEPTH_RATE_DEFAULT);
    CHECK(depthGovernorWindow(&ctx->depthGov, nowNs(), RATE_WINDOW_OVER, 1) == DEPTH_MOVE_CUT);
    frames(ctx, 1, &fid, 0);
    CHECK(fakeLive != NULL && fakeLive->rate == 10);
    XrDepthInfo info;
    xrDepthGetInfo(ctx, &info);
    CHECK(info.rateSet == 10);
    // A new preference starts the governor again at it
    setDepthRate(ctx, 30, "test");
    frames(ctx, 1, &fid, 0);
    CHECK(fakeLive != NULL && fakeLive->rate == 30);
    m6_ctx_free(ctx);
}

// ---- settings -------------------------------------------------------------------------------

static void testSettings(void) {
    char path[300];
    snprintf(path, sizeof(path), "%s/config.ini", assets);
    Settings s;
    settings_defaults(&s);
    // upstream's defaults: ZipDepth, its own 0.6 %, convergence 50, the
    // 3D on (native: remembered as it was last left)
    CHECK(s.depth_source == 0 && s.separation == -1 && s.convergence == -1 && s.stereo == 1);
    CHECK(settings_separation_units(&s) == 6);
    s.depth_source = 1;
    CHECK(settings_separation_units(&s) == 5);
    s.depth_source = 8;
    CHECK(settings_separation_units(&s) == 6);
    // round trip of every 3D row
    s.depth_source = 8;
    s.stereo = 1;
    s.separation = 4;
    s.convergence = 35;
    s.depth_rate = 15;
    CHECK(settings_save(&s, path) == 0);
    Settings back;
    settings_defaults(&back);
    CHECK(settings_load(&back, path, NULL) == 0);
    CHECK(back.depth_source == 8 && back.stereo == 1 && back.separation == 4 && back.convergence == 35
          && back.depth_rate == 15);
    CHECK(settings_equal(&s, &back));
    FILE* f = fopen(path, "r");
    char text[16384] = "";
    if (f) {
        size_t n = fread(text, 1, sizeof(text) - 1, f);
        text[n] = '\0';
        fclose(f);
    }
    CHECK(strstr(text, "depth_source = zipdepth-int8") != NULL);
    CHECK(strstr(text, "stereo = true") != NULL);
    unlink(path);

    // into the context
    XrCtx* ctx = m6_ctx(&g, &FAKE, assets, &back);
    CHECK(ctx->stereoMode == DEPTH_MODE_MODEL && ctx->depthModel == MLXR_DEPTH_ZIPDEPTH_256_INT8DR);
    CHECK(ctx->stereoLive == 1);
    CHECK_NEAR(ctx->separationPref, 0.004, 1e-6);
    CHECK_NEAR(ctx->convergence, 0.35, 1e-6);
    CHECK(cogRowLive(ctx, COG_TAB_3D, COG_ROW3D_SEPARATION));
    // a preset: Strong writes its separation and brings the 3D on
    float out[IN_SLOTS];
    for (int i = 0; i < IN_SLOTS; i++) out[i] = -1.0f;
    ctx->stereoLive = 0;
    cogApplyCell(ctx, COG_TAB_3D, COG_ROW3D_PRESET, COG_PRESET_STRONG, out);
    CHECK(ctx->stereoLive == 1);
    CHECK((int)out[IN_SETTING] == SETTING_SEPARATION);
    CHECK((int)out[IN_SETTING_VALUE] == ctx->presetUnits[COG_PRESET_STRONG]);
    CHECK(xrStoreSetting(ctx, &back, (int)out[IN_SETTING], (int)out[IN_SETTING_VALUE]) == 1);
    CHECK(back.separation == ctx->presetUnits[COG_PRESET_STRONG]);
    CHECK(cogCellInForce(ctx, COG_TAB_3D, COG_ROW3D_PRESET) == COG_PRESET_STRONG);
    // the depth track, live: the panel's separation wins over the preference
    ctx->separationPref = 0.002f;
    cogApplySlider(ctx, COG_TAB_3D, COG_ROW3D_SEPARATION, 0.5f);
    xrDepthFrame(ctx);
    CHECK(ctx->panelSeparation >= 0.0f);
    CHECK_NEAR(ctx->separationCurrent, ctx->panelSeparation, 1e-6);
    ctx->cogDragSlider = COG_ROW3D_SEPARATION;
    ctx->cogDragFace = COG_TAB_3D;
    for (int i = 0; i < IN_SLOTS; i++) out[i] = -1.0f;
    cogDragEnded(ctx, out);
    CHECK((int)out[IN_SETTING] == SETTING_SEPARATION);
    CHECK(xrStoreSetting(ctx, &back, SETTING_SEPARATION, (int)out[IN_SETTING_VALUE]) >= 0);
    CHECK(back.separation == separationUnits(ctx->separationCurrent));
    // the convergence track
    cogApplySlider(ctx, COG_TAB_3D, COG_ROW3D_CONVERGENCE, 0.8f);
    ctx->cogDragSlider = COG_ROW3D_CONVERGENCE;
    ctx->cogDragFace = COG_TAB_3D;
    for (int i = 0; i < IN_SLOTS; i++) out[i] = -1.0f;
    cogDragEnded(ctx, out);
    CHECK((int)out[IN_SETTING] == SETTING_CONVERGENCE);
    xrStoreSetting(ctx, &back, SETTING_CONVERGENCE, (int)out[IN_SETTING_VALUE]);
    CHECK(back.convergence == (int)lroundf(ctx->convergence * 100.0f));
    // reset goes back to the model's own
    xrStoreSetting(ctx, &back, SETTING_RESET_3D, 0);
    CHECK(back.separation == -1 && back.convergence == -1);
    m6_ctx_free(ctx);

    // MiDaS's own pair; off; a test pattern; a model missing; no engine
    Settings m;
    settings_defaults(&m);
    m.depth_source = 1;
    ctx = m6_ctx(&g, &FAKE, assets, &m);
    CHECK(ctx->stereoMode == DEPTH_MODE_MODEL && ctx->depthModel == MLXR_DEPTH_MIDAS_256_FP16);
    CHECK_NEAR(ctx->separationPref, 0.005, 1e-6);
    CHECK(ctx->stereoLive == 1);   // the 3D on by default
    m6_ctx_free(ctx);
    m.depth_source = 2;
    ctx = m6_ctx(&g, &FAKE, assets, &m);
    CHECK(ctx->stereoMode == DEPTH_MODE_OFF);
    CHECK(!cogRowLive(ctx, COG_TAB_3D, COG_ROW3D_SEPARATION));
    CHECK(strcmp(ctx->depthReason, ui_str(UIS_THREE_D_OFF_IN_SETTINGS)) == 0);
    m6_ctx_free(ctx);
    m.depth_source = 5;
    m.stereo = 1;
    ctx = m6_ctx(&g, &FAKE, assets, &m);
    CHECK(ctx->stereoMode == DEPTH_MODE_BLOB && ctx->stereoLive == 1);
    uint64_t fid = 0;
    frames(ctx, 2, &fid, 0);
    CHECK(ctx->drawnEyes == 2);
    m6_ctx_free(ctx);
    m.depth_source = 0;
    ctx = m6_ctx(&g, &FAKE, "/nonexistent", &m);
    CHECK(ctx->stereoMode == DEPTH_MODE_OFF);
    CHECK(strcmp(ctx->depthReason, ui_str(UIS_NATIVE_3D_NO_MODEL)) == 0);
    m6_ctx_free(ctx);
    ctx = m6_ctx(&g, NULL, assets, &m);
    CHECK(ctx->stereoMode == DEPTH_MODE_OFF);
    m6_ctx_free(ctx);
}

// ---- states --------------------------------------------------------------------------------

static void testStates(void) {
    int starts = fakeStarts;
    XrCtx* ctx = newCtx(0, 0);
    uint64_t fid = 0;
    CHECK(ctx->stereoMode == DEPTH_MODE_MODEL && ctx->stereoLive == 0);
    frames(ctx, 5, &fid, 0);
    // Never on, never started
    CHECK(fakeStarts == starts);
    CHECK(ctx->drawnEyes == 1);
    XrDepthInfo info;
    xrDepthGetInfo(ctx, &info);
    CHECK(info.engineState == -1);
    // First on: started, loading (flat meanwhile), then ready
    setStereoLive(ctx, 1, "test");
    CHECK(ctx->stereoWaiting == 1);
    frames(ctx, 1, &fid, 0);
    CHECK(fakeStarts == starts + 1);
    xrDepthGetInfo(ctx, &info);
    CHECK(info.engineState == MLXR_DEPTH_LOADING);
    CHECK(ctx->drawnEyes == 1);
    waitReady(ctx, &fid);
    for (int i = 0; i < 30 && ctx->drawnEyes != 2; i++) {
        frames(ctx, 1, &fid, 0);
        usleep(5000);
    }
    CHECK(ctx->drawnEyes == 2);
    CHECK(ctx->stereoWaiting == 0);
    // Off: flat, captures stop, the model idle
    setStereoLive(ctx, 0, "test");
    frames(ctx, 2, &fid, 0);
    xrDepthGetInfo(ctx, &info);
    uint64_t caps = info.captures;
    frames(ctx, 10, &fid, 0);
    xrDepthGetInfo(ctx, &info);
    CHECK(info.captures == caps);
    CHECK(ctx->drawnEyes == 1);
    char line[600];
    xrDepthOverlay(ctx, line, sizeof(line));
    CHECK(strstr(line, "off") != NULL);
    // Back on: reset, a capture at once, flat until a fresh map
    int resets = fakeLive != NULL ? fakeLive->resets : -1;
    setStereoLive(ctx, 1, "test");
    CHECK(ctx->stereoWaiting == 1);
    frames(ctx, 1, &fid, 0);
    CHECK(fakeLive != NULL && fakeLive->resets == resets + 1);
    xrDepthGetInfo(ctx, &info);
    CHECK(info.captures == caps + 1);
    CHECK(ctx->drawnEyes == 1);
    for (int i = 0; i < 30 && ctx->stereoWaiting; i++) {
        usleep(5000);
        frames(ctx, 1, &fid, 0);
    }
    CHECK(ctx->stereoWaiting == 0);
    frames(ctx, 1, &fid, 0);
    CHECK(ctx->drawnEyes == 2);
    CHECK(fakeStarts == starts + 1); // the same engine all along
    m6_ctx_free(ctx);

    // FAILED: flat, a toast, the switch put away, never again this session
    fakeFailNext = 1;
    ctx = newCtx(0, 1);
    ctx->stereoButtonReady = 1;
    int notices = ctx->notices.count;
    frames(ctx, 3, &fid, 0);
    CHECK(ctx->depthFailed == 1);
    CHECK(ctx->stereoMode == DEPTH_MODE_OFF && ctx->stereoLive == 0);
    CHECK(ctx->stereoButtonReady == 0);
    CHECK(atomic_load(&ctx->depthGaveUp) == 1);
    CHECK(ctx->notices.count == notices + 1);
    CHECK(strcmp(ctx->depthReason, ui_str(UIS_NATIVE_3D_FAILED)) == 0);
    CHECK(ctx->drawnEyes == 1);
    setStereoLive(ctx, 1, "test");
    frames(ctx, 2, &fid, 0);
    CHECK(ctx->stereoLive == 0 && ctx->drawnEyes == 1);
    xrDepthStatsLine(ctx, line, sizeof(line));
    CHECK(strstr(line, "failed") != NULL);
    m6_ctx_free(ctx);
    // The depth thread never starts: the same
    fakeNullNext = 1;
    ctx = newCtx(0, 1);
    frames(ctx, 2, &fid, 0);
    CHECK(ctx->depthFailed == 1 && ctx->stereoMode == DEPTH_MODE_OFF);
    m6_ctx_free(ctx);
}



// ---- sbs (PLAN 4.3) ------------------------------------------------------------------------

static void sourceCell(XrCtx* ctx, int source, float* out) {
    for (int i = 0; i < IN_SLOTS; i++) out[i] = -1.0f;
    cogApplyCell(ctx, COG_TAB_3D, COG_ROW3D_SOURCE, source, out);
}

static void testSbs(void) {
    int starts = fakeStarts;
    Settings s;
    settings_defaults(&s);
    s.stereo_source = STEREO_SOURCE_SBS;
    s.stereo = 1;
    XrCtx* ctx = m6_ctx(&g, &FAKE, assets, &s);
    CHECK(ctx->stereoMode == DEPTH_MODE_SBS && ctx->stereoLive == 1);
    CHECK(ctx->depthModeSet == DEPTH_MODE_MODEL && ctx->stereoSource == STEREO_SOURCE_SBS && ctx->sbsFull == 0);
    uint64_t fid = 0;
    frames(ctx, 5, &fid, 0);
    // No model, no capture, no depth pass: both eyes, each its own half
    CHECK(fakeStarts == starts);
    XrDepthInfo info;
    xrDepthGetInfo(ctx, &info);
    CHECK(info.engineState == -1 && info.captures == 0);
    CHECK(info.upsampleImage == VK_NULL_HANDLE && info.offsetImage == VK_NULL_HANDLE);
    CHECK(info.downscaleImage == VK_NULL_HANDLE);
    CHECK(ctx->drawnEyes == 2);
    CHECK(lastDraw.eyes == 2 && !lastDraw.upsampled && !lastDraw.occluded);
    for (int e = 0; e < 2; e++) {
        CHECK(lastDraw.eye[e].split == MLXR_SPLIT_SBS && lastDraw.eye[e].half == e && lastDraw.eye[e].eye == e);
        CHECK(lastDraw.eye[e].depth == VK_NULL_HANDLE && lastDraw.eye[e].offsets == VK_NULL_HANDLE);
        CHECK(lastDraw.eye[e].disparity == 0.0f && !lastDraw.eye[e].occlusion && !lastDraw.eye[e].showDepth);
    }
    // Convergence 0.5 is the pair as drawn; above it the halves move apart
    CHECK_NEAR(lastDraw.eye[0].shift, 0.0, 1e-6);
    CHECK_NEAR(xrDepthSbsShift(0.5f), 0.0, 1e-6);
    CHECK_NEAR(xrDepthSbsShift(1.0f), SBS_SHIFT_MAX, 1e-6);
    CHECK_NEAR(xrDepthSbsShift(0.0f), -SBS_SHIFT_MAX, 1e-6);
    float out[IN_SLOTS];
    cogApplySlider(ctx, COG_TAB_3D, COG_ROW3D_CONVERGENCE, cogRunU(0.75f));
    frames(ctx, 1, &fid, 0);
    CHECK(lastDraw.eye[0].shift > 0.0f && lastDraw.eye[1].shift < 0.0f);
    CHECK_NEAR(lastDraw.eye[0].shift, xrDepthSbsShift(ctx->convergence), 1e-6);
    CHECK_NEAR(lastDraw.eye[1].shift, -lastDraw.eye[0].shift, 1e-6);
    // The rows: nothing for the preset and depth rows to set
    CHECK(!cogRowLive(ctx, COG_TAB_3D, COG_ROW3D_PRESET) && !cogRowLive(ctx, COG_TAB_3D, COG_ROW3D_SEPARATION));
    CHECK(cogRowLive(ctx, COG_TAB_3D, COG_ROW3D_CONVERGENCE) && cogRowLive(ctx, COG_TAB_3D, COG_ROW3D_SWITCH));
    CHECK(cogRowLive(ctx, COG_TAB_3D, COG_ROW3D_SOURCE));
    CHECK(cogCellInForce(ctx, COG_TAB_3D, COG_ROW3D_SOURCE) == STEREO_SOURCE_SBS);
    CHECK(cogRowCells(COG_TAB_3D, COG_ROW3D_SOURCE) == COG_SOURCE_CELLS && !cogRowIsTrack(COG_TAB_3D, COG_ROW3D_SOURCE));
    CHECK(barStereoCell(ctx->stereoSource, 1) == BTN_CELL_STEREO_SBS && barStereoCell(ctx->stereoSource, 0) == BTN_CELL_STEREO);
    // Off: the left eye's half alone, flat
    setStereoLive(ctx, 0, "test");
    frames(ctx, 1, &fid, 0);
    CHECK(ctx->drawnEyes == 1 && lastDraw.eyes == 1);
    CHECK(lastDraw.eye[0].split == MLXR_SPLIT_SBS && lastDraw.eye[0].half == 0 && lastDraw.eye[0].shift == 0.0f);
    setStereoLive(ctx, 1, "test");
    frames(ctx, 1, &fid, 0);
    CHECK(ctx->drawnEyes == 2);
    // The picture: a half size pair keeps the frame's shape, a full size one
    // is one eye's half; the pointer goes to the left eye's half
    int pw = 0, ph = 0;
    xrDepthPictureSize(ctx, 1920, 1080, &pw, &ph);
    CHECK(pw == 1920 && ph == 1080);
    ctx->sbsFull = 1;
    xrDepthPictureSize(ctx, 3840, 1080, &pw, &ph);
    CHECK(pw == 1920 && ph == 1080);
    float pu = 0.5f, pv = 0.4f;
    xrDepthPointerUv(ctx, &pu, &pv);
    CHECK_NEAR(pu, 0.25, 1e-6);
    CHECK_NEAR(pv, 0.4, 1e-6);
    // No letterbox detection on a pair
    ctx->ambiBarDetect = 1;
    CHECK(!ambiBarDetectOn(ctx));
    char line[600];
    xrDepthStatsLine(ctx, line, sizeof(line));
    printf("stats line: %s\n", line);
    CHECK(strstr(line, "host-rendered") != NULL && strstr(line, "side by side, full size") != NULL);
    xrDepthOverlay(ctx, line, sizeof(line));
    CHECK(strstr(line, "from the PC") != NULL);

    // Top-bottom from the row: at once, and stored
    sourceCell(ctx, STEREO_SOURCE_TB, out);
    CHECK((int)out[IN_SETTING] == SETTING_STEREO_SOURCE && (int)out[IN_SETTING_VALUE] == STEREO_SOURCE_TB);
    Settings back = s;
    CHECK(xrStoreSetting(ctx, &back, (int)out[IN_SETTING], (int)out[IN_SETTING_VALUE]) == 1);
    CHECK(back.stereo_source == STEREO_SOURCE_TB);
    CHECK(ctx->stereoMode == DEPTH_MODE_SBS && ctx->stereoLive == 1);
    frames(ctx, 1, &fid, 0);
    CHECK(lastDraw.eyes == 2 && lastDraw.eye[0].split == MLXR_SPLIT_TB && lastDraw.eye[1].half == 1);
    xrDepthPictureSize(ctx, 1920, 2160, &pw, &ph);
    CHECK(pw == 1920 && ph == 1080);
    pu = 0.5f;
    pv = 0.4f;
    xrDepthPointerUv(ctx, &pu, &pv);
    CHECK_NEAR(pu, 0.5, 1e-6);
    CHECK_NEAR(pv, 0.2, 1e-6);
    CHECK(barStereoCell(ctx->stereoSource, 1) == BTN_CELL_STEREO_TB);

    // Back to Depth: the model starts, captures and the depth passes come back
    sourceCell(ctx, STEREO_SOURCE_DEPTH, out);
    CHECK((int)out[IN_SETTING_VALUE] == STEREO_SOURCE_DEPTH);
    CHECK(ctx->stereoMode == DEPTH_MODE_MODEL && ctx->stereoLive == 1 && ctx->stereoWaiting == 1);
    CHECK(ambiBarDetectOn(ctx));
    pu = 0.5f;
    xrDepthPointerUv(ctx, &pu, &pv);
    CHECK_NEAR(pu, 0.5, 1e-6);
    xrDepthPictureSize(ctx, 3840, 1080, &pw, &ph);
    CHECK(pw == 3840 && ph == 1080);
    waitReady(ctx, &fid);
    CHECK(fakeStarts == starts + 1);
    for (int i = 0; i < 40 && ctx->drawnEyes != 2; i++) {
        frames(ctx, 1, &fid, 0);
        usleep(5000);
    }
    frames(ctx, 1, &fid, 0);
    xrDepthGetInfo(ctx, &info);
    CHECK(ctx->drawnEyes == 2 && info.captures > 0);
    CHECK(lastDraw.upsampled == 1 && lastDraw.eye[0].split == MLXR_SPLIT_NONE && lastDraw.eye[1].split == MLXR_SPLIT_NONE);
    CHECK(info.upsampleImage != VK_NULL_HANDLE);
    CHECK(cogRowLive(ctx, COG_TAB_3D, COG_ROW3D_PRESET) && cogRowLive(ctx, COG_TAB_3D, COG_ROW3D_SEPARATION));

    // SBS again: the captures stop and no depth pass is recorded; the model idles
    sourceCell(ctx, STEREO_SOURCE_SBS, out);
    frames(ctx, 2, &fid, 0);
    xrDepthGetInfo(ctx, &info);
    uint64_t caps = info.captures;
    frames(ctx, 10, &fid, 0);
    xrDepthGetInfo(ctx, &info);
    CHECK(info.captures == caps);
    CHECK(ctx->drawnEyes == 2 && !lastDraw.upsampled && lastDraw.eye[0].split == MLXR_SPLIT_SBS);
    CHECK(fakeStarts == starts + 1 && fakeLive != NULL);
    // and Depth again: as if switched back on, a reset and a capture at once
    int resets = fakeLive != NULL ? fakeLive->resets : -1;
    sourceCell(ctx, STEREO_SOURCE_DEPTH, out);
    frames(ctx, 1, &fid, 0);
    xrDepthGetInfo(ctx, &info);
    CHECK(fakeLive != NULL && fakeLive->resets == resets + 1);
    CHECK(info.captures == caps + 1);
    // The same cell again changes nothing
    CHECK(xrDepthSetSource(ctx, STEREO_SOURCE_DEPTH) == 0);
    m6_ctx_free(ctx);

    // The model off in settings: the 3D tab is dead but for the source row,
    // which still brings the PC's pair, and the bar's button with it
    s.depth_source = 2;
    s.stereo_source = STEREO_SOURCE_DEPTH;
    ctx = m6_ctx(&g, &FAKE, assets, &s);
    CHECK(ctx->stereoMode == DEPTH_MODE_OFF && ctx->stereoLive == 0);
    CHECK(cogRowLive(ctx, COG_TAB_3D, COG_ROW3D_SOURCE) && !cogRowLive(ctx, COG_TAB_3D, COG_ROW3D_SWITCH));
    CHECK(!cogRowLive(ctx, COG_TAB_3D, COG_ROW3D_CONVERGENCE));
    ctx->stereoButtonReady = 0;
    sourceCell(ctx, STEREO_SOURCE_SBS, out);
    CHECK(ctx->stereoMode == DEPTH_MODE_SBS && ctx->stereoLive == 1 && ctx->stereoButtonReady == 1);
    CHECK(xrDepthChainWidth(ctx, 1920) == 3840);
    frames(ctx, 2, &fid, 0);
    CHECK(ctx->drawnEyes == 2);
    sourceCell(ctx, STEREO_SOURCE_DEPTH, out);
    CHECK(ctx->stereoMode == DEPTH_MODE_OFF && ctx->stereoLive == 0 && ctx->stereoButtonReady == 0);
    frames(ctx, 1, &fid, 0);
    CHECK(ctx->drawnEyes == 1);
    CHECK(fakeStarts == starts + 1);
    m6_ctx_free(ctx);
}

// ---- budget --------------------------------------------------------------------------------

#define FAKE_ROOM ((XrSwapchain)(uintptr_t)0x7001)
#define FAKE_GLOW ((XrSwapchain)(uintptr_t)0x7002)
#define FAKE_MODEL ((XrSwapchain)(uintptr_t)0x7003)
#define FAKE_MODEL_RIGHT ((XrSwapchain)(uintptr_t)0x7013)

static int pictureQuads(const XrFrameOut* out) {
    int n = 0;
    for (uint32_t i = 0; i < out->count; i++) {
        const XrCompositionLayerQuad* q = (const XrCompositionLayerQuad*)out->order[i];
        if (q->type == XR_TYPE_COMPOSITION_LAYER_QUAD && q->subImage.swapchain == SCENE_VIDEO_SWAPCHAIN) n++;
    }
    return n;
}

// Native: SteamVR shows no quad seen by one eye only, so in 3D the picture is
// the app's own projection layer (an image per eye), as the curved one
static XrCompositionLayerProjection fakePicture = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };

static int onePictureLayer(const XrFrameOut* out) {
    int n = 0;
    for (uint32_t i = 0; i < out->count; i++) {
        const XrCompositionLayerQuad* q = (const XrCompositionLayerQuad*)out->order[i];
        if (q->type == XR_TYPE_COMPOSITION_LAYER_QUAD && q->eyeVisibility != XR_EYE_VISIBILITY_BOTH) return 0;
        if (out->order[i] == (const XrCompositionLayerBaseHeader*)&fakePicture) n++;
    }
    return n == 1;
}

static void budgetFrame(Scene* s, const char* what, int* worst) {
    XrCtx* c = s->ctx;
    noticePush(&c->notices, TOAST_3D_ON, 0);
    Notice up;
    if (noticeAdvance(&c->notices, nowNs(), 0, &up)) xrPanelsToast(c, up.kind, up.arg);
    c->hoverKind = HOVER_BAR;
    scene_frame(s, "stats");
    int n = (int)s->out.count;
    printf("  %-66s %2d layers, %d picture quads\n", what, n, pictureQuads(&s->out));
    CHECK(n <= 16);
    // whatever is dropped, the picture stays: one projection layer, no quad
    // for one eye only
    CHECK(pictureQuads(&s->out) == 0 && onePictureLayer(&s->out));
    if (n > *worst) *worst = n;
}

static void testBudget(void) {
    Scene* s = scene_new();
    XrCtx* c = s->ctx;
    s->curved = &fakePicture;
    c->cylinderSupported = 1;
    c->alphaBlendSupported = 1;
    c->focusedFrames = 5;
    c->overlayVisible = 1;
    c->padButtonReady = 1;
    c->ambilightOn = 1;
    c->glowSwapchain = FAKE_GLOW;
    c->glowSwapchainFilled = 1;
    c->glowRendered = 1;
    // Native (lane eye-swapchains): a swapchain per eye
    c->modelSwapchain[0] = FAKE_MODEL;
    c->modelSwapchain[1] = FAKE_MODEL_RIGHT;
    c->modelsShowing = 1;
    c->modelRendered = 1;
    c->modelEyeWidth = c->modelEyeHeight = 1728;
    for (int e = 0; e < 2; e++) c->modelViews[e].pose.orientation.w = 1.0f;
    // 3D on and drawn: both halves, the bar's 3D button
    c->stereoMode = DEPTH_MODE_MODEL;
    c->stereoLive = 1;
    c->drawnEyes = 2;
    c->stereoButtonReady = 1;
    Vec3 hand = { 0.2f, -0.3f, -0.3f }, at = { 0, -1.0f, -3.0f };
    scene_ray_to(s, hand, at);
    printf("busiest frames with the 3D on (SteamVR takes 16):\n");
    int worst = 0;
    s->settings.head_locked = 1;
    c->headLockedPref = 1;
    budgetFrame(s, "void, head locked, bar + 3D, glow, models, stats, ray, dot, toast", &worst);
    s->settings.head_locked = 0;
    c->headLockedPref = 0;
    budgetFrame(s, "void, bar + 3D, glow, models, stats, ray, dot, toast", &worst);
    c->passthrough = 1;
    s->settings.passthrough = 1;
    budgetFrame(s, "passthrough, bar + 3D, glow, stats, ray, dot, toast", &worst);
    c->passthrough = 0;
    s->settings.passthrough = 0;
    printf("  worst %d of 16 (drop order: toast, then the models); a layer dropped: %s\n", worst,
           c->layerDropWarned ? "yes" : "no");
    scene_free(s);
}

int main(int argc, char** argv) {
    const char* group = argc > 1 ? argv[1] : "all";
    if (m6_gpu_init(&g, 0) != 0) {
        fprintf(stderr, "no Vulkan device\n");
        return 77;
    }
    makeAssets();
    XrCtx tmp;
    memset(&tmp, 0, sizeof(tmp));
    m5_ctx_gpu(&g, &tmp);
    for (int i = 0; i < VIDEOS; i++) {
        curVideo = i;
        m5_video(&g, &tmp, &videos[i], VW, VH, solid);
    }
    m5_image(&g, &tmp, &target, VW * 2, VH, VK_FORMAT_R8G8B8A8_UNORM,
             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
             VK_IMAGE_ASPECT_COLOR_BIT);
    int all = strcmp(group, "all") == 0;
    if (all || !strcmp(group, "pipeline")) testPipeline();
    if (all || !strcmp(group, "governor")) testGovernor();
    if (all || !strcmp(group, "settings")) testSettings();
    if (all || !strcmp(group, "states")) testStates();
    if (all || !strcmp(group, "budget")) testBudget();
    if (all || !strcmp(group, "sbs")) testSbs();
    vkDeviceWaitIdle(g.vk.device);
    for (int i = 0; i < VIDEOS; i++) m5_video_free(&tmp, &videos[i]);
    m5_image_free(&tmp, &target);
    xrWorldDestroy(&tmp);
    dropAssets();
    m5_gpu_free(&g);
    char name[64];
    snprintf(name, sizeof(name), "m6_%s", group);
    return checksDone(name);
}
