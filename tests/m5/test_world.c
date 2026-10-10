// Moonlight XR Native, tests/m5: the room, the glow and the models on the
// Frame's GPU through the ported code, headless. Groups:
//   leaks   rooms switched round and round through prepareRoom and the
//           loader thread (and a switch abandoned mid load): every Vulkan
//           image, view, buffer, allocation and command pool counted (the
//           create/destroy calls are wrapped here), steady from one round to
//           the next and back to the start once the world is destroyed
//   timing  GPU time of each pass at the Frame's recommended eye size
//           (1728 x 1728 a eye), from timestamps, median of 25
//   glow    the colour sample, the letterbox detection and the glow from a
//           letterboxed two colour picture; the redraw on a still picture
//   curve   the curved picture's image (UNORM since M5) gives the same bytes
//           through the panel pass as the flat picture; the M4 _SRGB image
//           did not
// GPLv3.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdlib.h>

#include "check.h"
#include "m5_gpu.h"
#include "xr_native.h"

// ---- Vulkan object counts (the libraries' calls bind to these) --------------------------

enum { OBJ_IMAGE, OBJ_VIEW, OBJ_BUFFER, OBJ_MEMORY, OBJ_POOL, OBJ_KINDS };
static long live[OBJ_KINDS];
static const char* const OBJ_NAMES[OBJ_KINDS] = { "images", "views", "buffers", "allocations", "command pools" };

#define NEXT(name) static PFN_##name next; if (!next) next = (PFN_##name)dlsym(RTLD_NEXT, #name)

VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(VkDevice d, const VkImageCreateInfo* i, const VkAllocationCallbacks* a,
                                             VkImage* o) {
    NEXT(vkCreateImage);
    VkResult r = next(d, i, a, o);
    if (r == VK_SUCCESS) live[OBJ_IMAGE]++;
    return r;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyImage(VkDevice d, VkImage o, const VkAllocationCallbacks* a) {
    NEXT(vkDestroyImage);
    if (o) live[OBJ_IMAGE]--;
    next(d, o, a);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView(VkDevice d, const VkImageViewCreateInfo* i,
                                                 const VkAllocationCallbacks* a, VkImageView* o) {
    NEXT(vkCreateImageView);
    VkResult r = next(d, i, a, o);
    if (r == VK_SUCCESS) live[OBJ_VIEW]++;
    return r;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(VkDevice d, VkImageView o, const VkAllocationCallbacks* a) {
    NEXT(vkDestroyImageView);
    if (o) live[OBJ_VIEW]--;
    next(d, o, a);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(VkDevice d, const VkBufferCreateInfo* i, const VkAllocationCallbacks* a,
                                              VkBuffer* o) {
    NEXT(vkCreateBuffer);
    VkResult r = next(d, i, a, o);
    if (r == VK_SUCCESS) live[OBJ_BUFFER]++;
    return r;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(VkDevice d, VkBuffer o, const VkAllocationCallbacks* a) {
    NEXT(vkDestroyBuffer);
    if (o) live[OBJ_BUFFER]--;
    next(d, o, a);
}
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(VkDevice d, const VkMemoryAllocateInfo* i,
                                                const VkAllocationCallbacks* a, VkDeviceMemory* o) {
    NEXT(vkAllocateMemory);
    VkResult r = next(d, i, a, o);
    if (r == VK_SUCCESS) live[OBJ_MEMORY]++;
    return r;
}
VKAPI_ATTR void VKAPI_CALL vkFreeMemory(VkDevice d, VkDeviceMemory o, const VkAllocationCallbacks* a) {
    NEXT(vkFreeMemory);
    if (o) live[OBJ_MEMORY]--;
    next(d, o, a);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateCommandPool(VkDevice d, const VkCommandPoolCreateInfo* i,
                                                   const VkAllocationCallbacks* a, VkCommandPool* o) {
    NEXT(vkCreateCommandPool);
    VkResult r = next(d, i, a, o);
    if (r == VK_SUCCESS) live[OBJ_POOL]++;
    return r;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyCommandPool(VkDevice d, VkCommandPool o, const VkAllocationCallbacks* a) {
    NEXT(vkDestroyCommandPool);
    if (o) live[OBJ_POOL]--;
    next(d, o, a);
}

static void counts(long* out) {
    memcpy(out, live, sizeof(live));
}

static int sameCounts(const long* a, const long* b, const char* what) {
    int same = 1;
    for (int k = 0; k < OBJ_KINDS; k++) {
        if (a[k] != b[k]) {
            fprintf(stderr, "  %s: %s %ld -> %ld\n", what, OBJ_NAMES[k], a[k], b[k]);
            same = 0;
        }
    }
    return same;
}

static void printCounts(const char* what, const long* c) {
    printf("  %-40s images %ld, views %ld, buffers %ld, allocations %ld, command pools %ld\n", what, c[0], c[1], c[2],
           c[3], c[4]);
}

// ---- shared ---------------------------------------------------------------------------

static M5Gpu g;

static XrCtx* newCtx(void) {
    XrCtx* c = calloc(1, sizeof(XrCtx));
    xrCtxDefaults(c, 1920, 1080, 60);
    Settings st;
    settings_defaults(&st);
    xrApplySettings(c, &st);
    m5_ctx_gpu(&g, c);
    c->sessionState = XR_SESSION_STATE_FOCUSED;
    c->shouldRender = 1;
    return c;
}

typedef struct {
    M5Image colour, depth;
    int eyeW, eyeH;
    XrView views[2];
    double meanLuma;
} RoomShot;

static void drawRoomBody(XrCtx* ctx, VkCommandBuffer cmd, void* user) {
    RoomShot* r = user;
    mlxr_barrier(cmd, r->colour.img.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    mlxr_barrier(cmd, r->depth.img.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    xrRoomRecord(ctx, cmd, r->colour.img.view, VK_FORMAT_R8G8B8A8_SRGB, r->depth.img.view, r->eyeW, r->eyeH, r->views);
}

// ---- leaks ------------------------------------------------------------------------------

static void untilBuilt(XrCtx* c, int style, RoomShot* shot) {
    for (int f = 0; f < 30; f++) {
        m5_frame(&g, c, NULL, NULL);
        if (style == 0 || (c->roomBuiltStyle == style && xrRoomResident(c) == style)) {
            break;
        }
        xrRoomWaitLoader(c);
    }
    CHECK(roomEffective(c) == 0 || c->roomBuiltStyle == style);
    if (style > 0 && shot != NULL) {
        m5_frame(&g, c, drawRoomBody, shot);
        uint8_t* px = malloc((size_t)shot->eyeW * 2 * shot->eyeH * 4);
        m5_readback(&g, shot->colour.img.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, shot->eyeW * 2, shot->eyeH,
                    px);
        double sum = 0;
        for (int i = 0; i < shot->eyeW * 2 * shot->eyeH; i++) sum += px[i * 4] + px[i * 4 + 1] + px[i * 4 + 2];
        shot->meanLuma = sum / (3.0 * shot->eyeW * 2 * shot->eyeH);
        free(px);
        CHECK(shot->meanLuma > 2.0); // something drawn, not the clear alone
    }
}

static void leaksGroup(void) {
    long pre[OBJ_KINDS], base[OBJ_KINDS], round[3][OBJ_KINDS], after[OBJ_KINDS];
    counts(pre);
    XrCtx* c = newCtx();
    RoomShot shot;
    memset(&shot, 0, sizeof(shot));
    shot.eyeW = shot.eyeH = 256;
    m5_image(&g, c, &shot.colour, 512, 256, VK_FORMAT_R8G8B8A8_SRGB,
             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    m5_image(&g, c, &shot.depth, 512, 256, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
             VK_IMAGE_ASPECT_DEPTH_BIT);
    shot.views[0] = m5_view(90.0f, 256, 256, -0.032f);
    shot.views[1] = m5_view(90.0f, 256, 256, 0.032f);
    long withShot[OBJ_KINDS];
    counts(withShot);
    m5_frame(&g, c, NULL, NULL);
    CHECK(c->ambiReady);
    counts(base);
    printCounts("before any room", base);
    const int cells[] = { ENV_CELL_HOME_THEATER, ENV_CELL_GRAND_CINEMA, ENV_CELL_SYNTHWAVE, ENV_CELL_VOID,
                          ENV_CELL_PASSTHROUGH };
    double luma[4] = { 0 };
    for (int r = 0; r < 3; r++) {
        for (unsigned i = 0; i < sizeof(cells) / sizeof(cells[0]); i++) {
            xrSetEnvironment(c, cells[i]);
            c->passthrough = cells[i] == ENV_CELL_PASSTHROUGH;
            int style = roomStyleForCell(cells[i]);
            untilBuilt(c, style, &shot);
            if (style > 0) luma[style] = shot.meanLuma;
        }
        counts(round[r]);
        char what[48];
        snprintf(what, sizeof(what), "after round %d (synthwave resident)", r + 1);
        printCounts(what, round[r]);
    }
    printf("  mean brightness of each room drawn (0..255): theater %.1f, cinema %.1f, synthwave %.1f\n", luma[1],
           luma[2], luma[3]);
    CHECK(sameCounts(round[0], round[1], "round 1 -> 2"));
    CHECK(sameCounts(round[1], round[2], "round 2 -> 3"));
    int retired = xrRoomsRetired(c);
    printf("  rooms freed %d, waiting %d\n", retired / 100, retired % 100);
    CHECK(retired / 100 >= 8 && retired % 100 == 0);

    // A room asked for and abandoned for the resident one mid load: the one
    // read is dropped without ever reaching the GPU
    xrSetEnvironment(c, ENV_CELL_SYNTHWAVE);
    untilBuilt(c, ROOM_STYLE_SYNTHWAVE, NULL);
    long before[OBJ_KINDS];
    counts(before);
    xrSetEnvironment(c, ENV_CELL_HOME_THEATER);
    m5_frame(&g, c, NULL, NULL); // asks for the theater
    xrSetEnvironment(c, ENV_CELL_SYNTHWAVE);
    xrRoomWaitLoader(c);         // the theater arrives, unwanted
    m5_frame(&g, c, NULL, NULL);
    CHECK(xrRoomResident(c) == ROOM_STYLE_SYNTHWAVE && c->roomBuiltStyle == ROOM_STYLE_SYNTHWAVE);
    counts(after);
    CHECK(sameCounts(before, after, "abandoned load"));

    // A rescale (upstream's scale property) rebuilds the vertex buffer and
    // frees its staging once the GPU is done
    c->roomScaleOverride = 1.5f;
    m5_frame(&g, c, NULL, NULL);
    CHECK(c->room != NULL && fabsf(c->room->scale - 1.5f) < 1e-4f);
    m5_frame(&g, c, NULL, NULL);
    counts(after);
    CHECK(sameCounts(before, after, "rescale"));
    c->roomScaleOverride = -1.0f;
    m5_frame(&g, c, NULL, NULL);

    // The controller model, loaded once the switch is on
    c->modelOn = 1;
    m5_frame(&g, c, NULL, NULL);
    CHECK(c->modelReady && c->model != NULL);
    m5_frame(&g, c, NULL, NULL);

    m5_image_free(c, &shot.colour);
    m5_image_free(c, &shot.depth);
    xrWorldDestroy(c);
    counts(after);
    printCounts("after the world is destroyed", after);
    CHECK(sameCounts(pre, after, "destroyed"));
    free(c);
    (void)withShot;
}

// ---- timing -----------------------------------------------------------------------------

typedef struct {
    VkQueryPool q;
    double tickNs;
} Timer;

static int cmpd(const void* a, const void* b) {
    double x = *(const double*)a, y = *(const double*)b;
    return x < y ? -1 : x > y;
}

typedef void (*PassFn)(XrCtx* ctx, VkCommandBuffer cmd, void* user);

static double timePass(XrCtx* c, Timer* t, PassFn fn, void* user) {
    double ms[25];
    for (int i = 0; i < 27; i++) {
        VkCommandBuffer cmd = m5_begin(&g);
        c->frameCmd = cmd;
        vkCmdResetQueryPool(cmd, t->q, 0, 2);
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, t->q, 0);
        fn(c, cmd, user);
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, t->q, 1);
        c->frameCmd = VK_NULL_HANDLE;
        m5_submit(&g, cmd, NULL, 0);
        uint64_t ticks[2];
        vkGetQueryPoolResults(g.vk.device, t->q, 0, 2, sizeof(ticks), ticks, 8,
                              VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
        if (i >= 2) ms[i - 2] = (ticks[1] - ticks[0]) * t->tickNs / 1e6; // two warm ups
    }
    qsort(ms, 25, sizeof(double), cmpd);
    return ms[12];
}

static void modelsBody(XrCtx* ctx, VkCommandBuffer cmd, void* user) {
    RoomShot* r = user;
    mlxr_barrier(cmd, r->colour.img.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    mlxr_barrier(cmd, r->depth.img.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    xrModelRecord(ctx, cmd, r->colour.img.view, VK_FORMAT_R8G8B8A8_SRGB, r->depth.img.view, r->eyeW, r->eyeH,
                  r->views);
}

static M5Video video;

static void glowBody(XrCtx* ctx, VkCommandBuffer cmd, void* user) {
    (void)cmd;
    (void)user;
    xrWorldVideoFrame(ctx, &video.video);
}

typedef struct {
    M5Image target;
    M5Image src;
    int w, h;
} WarpShot;

static void warpBody(XrCtx* ctx, VkCommandBuffer cmd, void* user) {
    WarpShot* ws = user;
    mlxr_barrier(cmd, ws->target.img.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    MlxrTarget t;
    memset(&t, 0, sizeof(t));
    t.view = ws->target.img.view;
    t.format = ws->target.img.format;
    t.rect.extent = (VkExtent2D){ (uint32_t)ws->w, (uint32_t)ws->h };
    MlxrWarpParams w;
    memset(&w, 0, sizeof(w));
    w.video = video.video;
    w.convergence = 0.5f;
    w.srcInset = 0.5f / 1920.0f;
    w.edgeFade = 16.0f / 1920.0f;
    w.tint[0] = w.tint[1] = w.tint[2] = 1.0f;
    w.sharpen = 0.8f; // the Display tab's default, Quality
    mlxr_pass_warp(ctx->passes, cmd, &t, &w);
}

typedef struct {
    RoomShot* shot;
    VkImageView src;
} CurveShot;

static void curveBody(XrCtx* ctx, VkCommandBuffer cmd, void* user) {
    CurveShot* cs = user;
    RoomShot* r = cs->shot;
    mlxr_barrier(cmd, r->colour.img.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    MlxrTarget tg;
    memset(&tg, 0, sizeof(tg));
    tg.view = r->colour.img.view;
    tg.format = VK_FORMAT_R8G8B8A8_SRGB;
    tg.rect.extent = (VkExtent2D){ (uint32_t)r->eyeW * 2, (uint32_t)r->eyeH };
    tg.clear = 1;
    // The glow then the picture, as curveRecord draws them
    ctx->screenPose.orientation.w = 1.0f;
    ctx->screenPose.position.z = -2.5f;
    ctx->screenWidth = 3.0f;
    ctx->screenRadius = 4.0f;
    xrWorldCurvedGlow(ctx, &tg, r->views, r->eyeW, r->eyeH);
    tg.clear = 0;
    mlxr_barrier(cmd, r->colour.img.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    MlxrPanelParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.src = cs->src;
    pp.eyes = 2;
    pp.width = 3.0f;
    pp.height = 3.0f * 9.0f / 16.0f;
    pp.radius = 4.0f;
    pp.segments = 96;
    pp.color[0] = pp.color[1] = pp.color[2] = pp.color[3] = 1.0f;
    for (int e = 0; e < 2; e++) {
        xrCurveMvp(r->views[e].fov, r->views[e].pose, ctx->screenPose, pp.mvp[e]);
        pp.eyeRect[e].offset.x = e * r->eyeW;
        pp.eyeRect[e].extent = (VkExtent2D){ (uint32_t)r->eyeW, (uint32_t)r->eyeH };
        pp.uvRect[e][2] = pp.uvRect[e][3] = 1.0f;
    }
    mlxr_pass_panel(ctx->passes, cmd, &tg, &pp);
}

static void twoColour(float u, float v, float out[3]);

static void timingGroup(void) {
    XrCtx* c = newCtx();
    Timer t;
    VkQueryPoolCreateInfo qi = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
    qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qi.queryCount = 2;
    vkCreateQueryPool(g.vk.device, &qi, NULL, &t.q);
    t.tickNs = g.props.limits.timestampPeriod;
    m5_frame(&g, c, NULL, NULL);
    m5_video(&g, c, &video, 1920, 1080, twoColour);

    int eyeW, eyeH;
    worldEyeSize(c, &eyeW, &eyeH);
    CHECK(eyeW == 1728 && eyeH == 1728);
    RoomShot shot;
    memset(&shot, 0, sizeof(shot));
    shot.eyeW = eyeW;
    shot.eyeH = eyeH;
    m5_image(&g, c, &shot.colour, eyeW * 2, eyeH, VK_FORMAT_R8G8B8A8_SRGB,
             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    m5_image(&g, c, &shot.depth, eyeW * 2, eyeH, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
             VK_IMAGE_ASPECT_DEPTH_BIT);
    // The Frame's own field of view is about 100 degrees across a eye
    shot.views[0] = m5_view(100.0f, eyeW, eyeH, -0.032f);
    shot.views[1] = m5_view(100.0f, eyeW, eyeH, 0.032f);
    printf("GPU time a pass, median of 25 (Turnip Adreno 750, no compositor running):\n");

    // The glow chain at 1080p (the colour sample, the edge, the glow; the
    // letterbox detector's draw and copy every 30th frame)
    c->ambilightOn = 1;
    double glowMs = timePass(c, &t, glowBody, NULL);
    printf("  colour sample + glow edge + glow (1920x1080 in)        %.3f ms\n", glowMs);
    // The picture's warp, for scale: 1080p with Quality sharpening
    WarpShot ws;
    memset(&ws, 0, sizeof(ws));
    ws.w = 1920;
    ws.h = 1080;
    m5_image(&g, c, &ws.target, 1920, 1080, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
             VK_IMAGE_ASPECT_COLOR_BIT);
    double warpMs = timePass(c, &t, warpBody, &ws);
    printf("  warp 1920x1080, Quality sharpen (for comparison)        %.3f ms\n", warpMs);
    c->ambiSeeded = 1;
    c->roomLightOn = 1;
    const char* names[4] = { "", "home theater", "grand cinema", "synthwave" };
    for (int st = ROOM_STYLE_FIRST; st <= ROOM_STYLE_LAST; st++) {
        xrSetEnvironment(c, roomCellForStyle(st));
        untilBuilt(c, st, NULL);
        double ms = timePass(c, &t, drawRoomBody, &shot);
        printf("  room %-13s 2 x %dx%d, lit, ASTC atlas         %.3f ms\n", names[st], eyeW, eyeH, ms);
        CHECK(ms < 8.0);
    }
    c->modelOn = 1;
    m5_frame(&g, c, NULL, NULL);
    c->modelShown[0] = c->modelShown[1] = 1;
    Vec3 ax = { 1, 0, 0 };
    c->modelGrip[0] = (XrPosef){ axisAngleQuat(ax, -0.5f), { -0.18f, -0.22f, -0.45f } };
    c->modelGrip[1] = (XrPosef){ axisAngleQuat(ax, -0.5f), { 0.20f, -0.20f, -0.50f } };
    double modelMs = timePass(c, &t, modelsBody, &shot);
    printf("  controller models, both hands, 2 x %dx%d              %.3f ms\n", eyeW, eyeH, modelMs);
    CHECK(modelMs < 4.0);
    // The curved picture's layer with the glow drawn into it
    WarpShot pic;
    memset(&pic, 0, sizeof(pic));
    m5_image(&g, c, &pic.target, 1920, 1080, VK_FORMAT_R8G8B8A8_UNORM,
             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    CurveShot cs = { &shot, pic.target.img.view };
    {
        VkCommandBuffer cmd = m5_begin(&g);
        c->frameCmd = cmd;
        pic.w = 1920;
        pic.h = 1080;
        warpBody(c, cmd, &pic);
        mlxr_barrier(cmd, pic.target.img.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        c->frameCmd = VK_NULL_HANDLE;
        m5_submit(&g, cmd, NULL, 0);
    }
    double curveMs = timePass(c, &t, curveBody, &cs);
    printf("  curved picture + curved glow, 2 x %dx%d               %.3f ms\n", eyeW, eyeH, curveMs);
    m5_image_free(c, &pic.target);
    m5_image_free(c, &ws.target);
    m5_image_free(c, &shot.colour);
    m5_image_free(c, &shot.depth);
    m5_video_free(c, &video);
    vkDestroyQueryPool(g.vk.device, t.q, NULL);
    xrWorldDestroy(c);
    free(c);
}

// ---- glow ---------------------------------------------------------------------------------

// Red left, blue right, black bars over the top and bottom 15%
static void twoColour(float u, float v, float out[3]) {
    int bar = v < 0.15f || v > 0.85f;
    out[0] = bar ? 0.0f : (u < 0.5f ? 0.85f : 0.10f);
    out[1] = bar ? 0.0f : 0.10f;
    out[2] = bar ? 0.0f : (u < 0.5f ? 0.10f : 0.85f);
}

static void glowGroup(void) {
    XrCtx* c = newCtx();
    m5_frame(&g, c, NULL, NULL);
    m5_video(&g, c, &video, 1920, 1080, twoColour);
    c->ambilightOn = 1;
    c->ambiIntensity = 0.5f;
    for (int f = 0; f < AMBI_BAR_PERIOD * (AMBI_BAR_APPLY + 2); f++) {
        m5_frame(&g, c, glowBody, NULL);
    }
    printf("  bars applied: rows from the top %d, from the bottom %d, left %d, right %d; crop %.3f %.3f %.3f %.3f\n",
           c->ambiBarApplied[AMBI_EDGE_BOTTOM], c->ambiBarApplied[AMBI_EDGE_TOP], c->ambiBarApplied[AMBI_EDGE_LEFT],
           c->ambiBarApplied[AMBI_EDGE_RIGHT], c->ambiCrop[0], c->ambiCrop[1], c->ambiCrop[2], c->ambiCrop[3]);
    // 15% of 32 rows is 4.8: four whole rows of bar each end, no columns
    CHECK(c->ambiBarApplied[AMBI_EDGE_BOTTOM] >= 4 && c->ambiBarApplied[AMBI_EDGE_BOTTOM] <= 5);
    CHECK(c->ambiBarApplied[AMBI_EDGE_TOP] >= 4 && c->ambiBarApplied[AMBI_EDGE_TOP] <= 5);
    CHECK(c->ambiBarApplied[AMBI_EDGE_LEFT] == 0 && c->ambiBarApplied[AMBI_EDGE_RIGHT] == 0);
    CHECK(c->ambiCrop[3] < 0.8f && c->ambiCrop[3] > 0.6f);
    CHECK(c->glowRendered && c->glowDrawnOn && c->ambiSeeded);

    uint8_t* glow = malloc(GLOW_TEX * GLOW_TEX * 4);
    m5_readback(&g, c->glowTexture.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, GLOW_TEX, GLOW_TEX, glow);
    // Halfway down, a little in from each side: red on the left, blue on the
    // right, from the picture rather than its bars
    const uint8_t* l = glow + ((size_t)(GLOW_TEX / 2) * GLOW_TEX + GLOW_TEX / 10) * 4;
    const uint8_t* r = glow + ((size_t)(GLOW_TEX / 2) * GLOW_TEX + GLOW_TEX * 9 / 10) * 4;
    const uint8_t* top = glow + ((size_t)(GLOW_TEX / 10) * GLOW_TEX + GLOW_TEX / 4) * 4;
    printf("  glow at the left %d %d %d a %d, right %d %d %d a %d, top left quarter %d %d %d a %d\n", l[0], l[1], l[2],
           l[3], r[0], r[1], r[2], r[3], top[0], top[1], top[2], top[3]);
    CHECK(l[0] > 2 * l[2] && r[2] > 2 * r[0] && l[3] > 0 && r[3] > 0);
    // The top edge of the glow is lit from the picture's top row inside the
    // bars (red there), not black
    CHECK(top[0] > top[2]);

    // A still picture: the level moves, the glow is drawn again at it
    c->ambiIntensity = 0.9f;
    CHECK(glowStale(c));
    VkCommandBuffer cmd = m5_begin(&g);
    c->frameCmd = cmd;
    xrWorldStillFrame(c, &video.video);
    c->frameCmd = VK_NULL_HANDLE;
    m5_submit(&g, cmd, NULL, 0);
    CHECK(!glowStale(c) && c->glowDrawnLevel == 0.9f);
    uint8_t* brighter = malloc(GLOW_TEX * GLOW_TEX * 4);
    m5_readback(&g, c->glowTexture.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, GLOW_TEX, GLOW_TEX, brighter);
    const uint8_t* l2 = brighter + ((size_t)(GLOW_TEX / 2) * GLOW_TEX + GLOW_TEX / 10) * 4;
    CHECK(l2[3] > l[3]);
    // The stream goes: nothing sampled from it is kept
    xrWorldVideoGone(c);
    CHECK(!c->ambiSeeded && !c->glowRendered && !xrWorldGlowShown(c));
    free(glow);
    free(brighter);
    m5_video_free(c, &video);
    xrWorldDestroy(c);
    free(c);
}

// ---- curve --------------------------------------------------------------------------------

static void curveGroup(void) {
    XrCtx* c = newCtx();
    m5_frame(&g, c, NULL, NULL);
    m5_video(&g, c, &video, 1920, 1080, twoColour);
    const int W = 480, H = 270;
    // (a) the flat picture: the warp straight into an _SRGB image, as the
    // video swapchain; (b) M5: into a _UNORM image, then the panel pass flat
    // over the whole of an _SRGB target; (c) M4: an _SRGB intermediate
    WarpShot a, b, cimg;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    memset(&cimg, 0, sizeof(cimg));
    a.w = b.w = cimg.w = W;
    a.h = b.h = cimg.h = H;
    VkImageUsageFlags u = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    m5_image(&g, c, &a.target, W, H, VK_FORMAT_R8G8B8A8_SRGB, u, VK_IMAGE_ASPECT_COLOR_BIT);
    m5_image(&g, c, &b.target, W, H, VK_FORMAT_R8G8B8A8_UNORM, u, VK_IMAGE_ASPECT_COLOR_BIT);
    m5_image(&g, c, &cimg.target, W, H, VK_FORMAT_R8G8B8A8_SRGB, u, VK_IMAGE_ASPECT_COLOR_BIT);
    M5Image outB, outC;
    m5_image(&g, c, &outB, W, H, VK_FORMAT_R8G8B8A8_SRGB, u, VK_IMAGE_ASPECT_COLOR_BIT);
    m5_image(&g, c, &outC, W, H, VK_FORMAT_R8G8B8A8_SRGB, u, VK_IMAGE_ASPECT_COLOR_BIT);
    VkCommandBuffer cmd = m5_begin(&g);
    c->frameCmd = cmd;
    warpBody(c, cmd, &a);
    warpBody(c, cmd, &b);
    warpBody(c, cmd, &cimg);
    M5Image* outs[2] = { &outB, &outC };
    WarpShot* srcs[2] = { &b, &cimg };
    for (int k = 0; k < 2; k++) {
        mlxr_barrier(cmd, srcs[k]->target.img.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        mlxr_barrier(cmd, outs[k]->img.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        MlxrTarget t;
        memset(&t, 0, sizeof(t));
        t.view = outs[k]->img.view;
        t.format = VK_FORMAT_R8G8B8A8_SRGB;
        t.rect.extent = (VkExtent2D){ (uint32_t)W, (uint32_t)H };
        t.clear = 1;
        MlxrPanelParams pp;
        memset(&pp, 0, sizeof(pp));
        pp.src = srcs[k]->target.img.view;
        pp.eyes = 1;
        float id[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        memcpy(pp.mvp[0], id, sizeof(id));
        pp.eyeRect[0].extent = t.rect.extent;
        pp.uvRect[0][2] = pp.uvRect[0][3] = 1.0f;
        pp.width = 2.0f;
        pp.height = 2.0f;
        pp.segments = 1;
        pp.color[0] = pp.color[1] = pp.color[2] = pp.color[3] = 1.0f;
        mlxr_pass_panel(c->passes, cmd, &t, &pp);
    }
    c->frameCmd = VK_NULL_HANDLE;
    m5_submit(&g, cmd, NULL, 0);
    uint8_t* pa = malloc((size_t)W * H * 4);
    uint8_t* pb = malloc((size_t)W * H * 4);
    uint8_t* pc = malloc((size_t)W * H * 4);
    m5_readback(&g, a.target.img.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, W, H, pa);
    m5_readback(&g, outB.img.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, W, H, pb);
    m5_readback(&g, outC.img.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, W, H, pc);
    // Away from the panel's edge texels
    int maxB = 0, maxC = 0;
    for (int y = 2; y < H - 2; y++) {
        for (int x = 2; x < W - 2; x++) {
            for (int k = 0; k < 3; k++) {
                size_t i = ((size_t)y * W + x) * 4 + k;
                int db = abs(pa[i] - pb[i]), dc = abs(pa[i] - pc[i]);
                if (db > maxB) maxB = db;
                if (dc > maxC) maxC = dc;
            }
        }
    }
    size_t mid = ((size_t)(H / 2) * W + W / 4) * 4;
    printf("  flat picture %d %d %d; curved through UNORM %d %d %d (max diff %d); through SRGB (M4) %d %d %d "
           "(max diff %d)\n", pa[mid], pa[mid + 1], pa[mid + 2], pb[mid], pb[mid + 1], pb[mid + 2], maxB, pc[mid],
           pc[mid + 1], pc[mid + 2], maxC);
    CHECK(maxB <= 3);
    CHECK(maxC > 40);
    free(pa);
    free(pb);
    free(pc);
    m5_image_free(c, &a.target);
    m5_image_free(c, &b.target);
    m5_image_free(c, &cimg.target);
    m5_image_free(c, &outB);
    m5_image_free(c, &outC);
    m5_video_free(c, &video);
    xrWorldDestroy(c);
    free(c);
}

int main(int argc, char** argv) {
    const char* grp = argc > 1 ? argv[1] : "all";
    int all = !strcmp(grp, "all");
    if (m5_gpu_init(&g) != 0) {
        fprintf(stderr, "no Vulkan device\n");
        return 1;
    }
    if (all || !strcmp(grp, "leaks")) leaksGroup();
    if (all || !strcmp(grp, "timing")) timingGroup();
    if (all || !strcmp(grp, "glow")) glowGroup();
    if (all || !strcmp(grp, "curve")) curveGroup();
    m5_gpu_free(&g);
    char name[32];
    snprintf(name, sizeof(name), "m5 world %s", grp);
    return checksDone(name);
}
