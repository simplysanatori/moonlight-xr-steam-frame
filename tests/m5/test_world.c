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
//   frame_models  the controller model loader: the Steam Frame's per-hand pair (native: textured
//                 .model preferred over painted .room)
//           (written here as tiny meshes) when both parse, each hand its own
//           range and not mirrored, else the generic controller; a corrupt
//           pair with no generic one is a failed load, not a crash
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

// A time budget: failed on the Frame's GPU, printed only with MLXR_SOFT_GPU=1
// (lavapipe in CI draws on the CPU, so its times say nothing of the Frame's)
#define CHECK_BUDGET(cond) do { \
        if (m5_soft_gpu()) { \
            if (!(cond)) printf("  over budget (MLXR_SOFT_GPU=1, not failed): %s\n", #cond); \
        } \
        else CHECK(cond); \
    } while (0)

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
    // Native (lane eye-swapchains): an image per eye (as the session's
    // swapchains) instead of colour side by side; depth eyeW x eyeH then
    int perEye;
    M5Image eye[2];
} RoomShot;

// Where a shot is drawn, its images moved to COLOR_ATTACHMENT (and the depth)
static XrEyeTargets shotTargets(RoomShot* r, VkCommandBuffer cmd) {
    XrEyeTargets t;
    if (r->perEye) {
        VkImage im[2] = { r->eye[0].img.image, r->eye[1].img.image };
        VkImageView v[2] = { r->eye[0].img.view, r->eye[1].img.view };
        t = xrEyesPerImage(im, v, VK_FORMAT_R8G8B8A8_SRGB, r->eyeW, r->eyeH);
    }
    else {
        t = xrEyesSideBySide(r->colour.img.image, r->colour.img.view, VK_FORMAT_R8G8B8A8_SRGB, r->eyeW, r->eyeH);
    }
    for (int e = 0; e < (r->perEye ? 2 : 1); e++) {
        mlxr_barrier(cmd, t.image[e], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    }
    if (r->depth.img.image != VK_NULL_HANDLE) {
        mlxr_barrier(cmd, r->depth.img.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        t.depthImage = r->depth.img.image;
        t.depthView = r->depth.img.view;
    }
    return t;
}

static void drawRoomBody(XrCtx* ctx, VkCommandBuffer cmd, void* user) {
    RoomShot* r = user;
    XrEyeTargets t = shotTargets(r, cmd);
    xrRoomRecord(ctx, cmd, &t, r->views);
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
    XrEyeTargets t = shotTargets(r, cmd);
    xrModelRecord(ctx, cmd, &t, r->views);
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
    XrEyeTargets tg = shotTargets(r, cmd);
    tg.clear = 1;
    // The glow then the picture, as curveRecord draws them
    ctx->screenPose.orientation.w = 1.0f;
    ctx->screenPose.position.z = -2.5f;
    ctx->screenWidth = 3.0f;
    ctx->screenRadius = 4.0f;
    xrWorldCurvedGlow(ctx, &tg, r->views);
    tg.clear = 0;
    xrEyesBarrier(cmd, &tg);
    MlxrPanelParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.src = cs->src;
    pp.width = 3.0f;
    pp.height = 3.0f * 9.0f / 16.0f;
    pp.radius = 4.0f;
    pp.segments = 96;
    pp.color[0] = pp.color[1] = pp.color[2] = pp.color[3] = 1.0f;
    for (int e = 0; e < 2; e++) {
        xrCurveMvp(r->views[e].fov, r->views[e].pose, ctx->screenPose, pp.mvp[e]);
        pp.uvRect[e][2] = pp.uvRect[e][3] = 1.0f;
    }
    xrEyesPanel(ctx, cmd, &tg, &pp, NULL);
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
        CHECK_BUDGET(ms < 8.0);
    }
    // Native: one projection layer a frame draws the room into the 3D
    // picture's image, at the picture's eye size (2640 at the Frame's 90 Hz
    // recommendation), so the room's own cost at that size
    {
        RoomShot big;
        memset(&big, 0, sizeof(big));
        big.eyeW = big.eyeH = 2640;
        m5_image(&g, c, &big.colour, big.eyeW * 2, big.eyeH, VK_FORMAT_R8G8B8A8_SRGB,
                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        m5_image(&g, c, &big.depth, big.eyeW * 2, big.eyeH, VK_FORMAT_D32_SFLOAT,
                 VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
        big.views[0] = m5_view(100.0f, big.eyeW, big.eyeH, -0.032f);
        big.views[1] = m5_view(100.0f, big.eyeW, big.eyeH, 0.032f);
        for (int st = ROOM_STYLE_FIRST; st <= ROOM_STYLE_LAST; st++) {
            xrSetEnvironment(c, roomCellForStyle(st));
            untilBuilt(c, st, NULL);
            double ms = timePass(c, &t, drawRoomBody, &big);
            printf("  room %-13s 2 x 2640x2640 (in the picture's layer) %.3f ms\n", names[st], ms);
            CHECK_BUDGET(ms < 8.0);
        }
        m5_image_free(c, &big.colour);
        m5_image_free(c, &big.depth);
    }
    c->modelOn = 1;
    m5_frame(&g, c, NULL, NULL);
    c->modelShown[0] = c->modelShown[1] = 1;
    Vec3 ax = { 1, 0, 0 };
    c->modelGrip[0] = (XrPosef){ axisAngleQuat(ax, -0.5f), { -0.18f, -0.22f, -0.45f } };
    c->modelGrip[1] = (XrPosef){ axisAngleQuat(ax, -0.5f), { 0.20f, -0.20f, -0.50f } };
    double modelMs = timePass(c, &t, modelsBody, &shot);
    printf("  controller models, both hands, 2 x %dx%d              %.3f ms\n", eyeW, eyeH, modelMs);
    CHECK_BUDGET(modelMs < 4.0);
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
    long overB = 0;
    for (int y = 2; y < H - 2; y++) {
        for (int x = 2; x < W - 2; x++) {
            for (int k = 0; k < 3; k++) {
                size_t i = ((size_t)y * W + x) * 4 + k;
                int db = abs(pa[i] - pb[i]), dc = abs(pa[i] - pc[i]);
                if (db > maxB) maxB = db;
                if (dc > maxC) maxC = dc;
                if (db > 3) overB++;
            }
        }
    }
    size_t mid = ((size_t)(H / 2) * W + W / 4) * 4;
    printf("  flat picture %d %d %d; curved through UNORM %d %d %d (max diff %d); through SRGB (M4) %d %d %d "
           "(max diff %d)\n", pa[mid], pa[mid + 1], pa[mid + 2], pb[mid], pb[mid + 1], pb[mid + 2], maxB, pc[mid],
           pc[mid + 1], pc[mid + 2], maxC);
    // lavapipe interpolates the curved mesh's UVs a hair apart from the flat
    // quad's, so the picture's hard colour edges land a texel over (max diff
    // 18 seen, Mesa 25.2): with MLXR_SOFT_GPU=1 only a few such samples pass
    printf("  curved through UNORM: %ld samples off by more than 3\n", overB);
    if (m5_soft_gpu()) CHECK(overB * 100 < (long)W * H * 3);
    else CHECK(maxB <= 3);
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


// ---- supersample --------------------------------------------------------------------------
// Upstream asked the compositor to supersample the picture's layer
// (XR_FB_composition_layer_settings); SteamVR has no such setting. This
// measures what the app side pass would bring if SteamVR samples a quad
// layer the plain way, bilinear from a one level image (its swapchain has no
// mips): the picture 2560x1440 at the default placement (3 m wide, 3 m away)
// covers about 918 of the eye's pixels across (1728 over ~100 degrees), a
// minification of 2.79. Compared against the ideal (each display pixel the
// area average of the picture under it), at a sub pixel phase the compositor
// does not share with the app: (A) bilinear straight from the picture, (B)
// the supersample pass to the footprint, then bilinear 1:1.

#define SS_W 2560
#define SS_H 1440

static uint32_t hashu(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352d;
    x ^= x >> 15;
    x *= 0x846ca68b;
    x ^= x >> 16;
    return x;
}

// Left: a zone plate sweeping up to the picture's Nyquist; right: desktop
// text, 1 px strokes in 6 x 10 cells
static float ssSource(int x, int y) {
    if (x < SS_W / 2) {
        float dx = x - SS_W / 4.0f, dy = y - SS_H / 2.0f;
        float r2 = dx * dx + dy * dy;
        return 0.5f + 0.5f * cosf((float)M_PI * r2 / (SS_H / 2.0f) * 0.5f);
    }
    int cx = x / 6, cy = y / 10, ix = x % 6, iy = y % 10;
    if (ix == 5 || iy >= 8 || (cy % 3) == 2) return 0.12f;  // gaps and line spacing
    uint32_t h = hashu((uint32_t)cx * 7919u + (uint32_t)cy * 104729u);
    int stroke = (ix == (int)(h % 5)) || (iy == (int)((h >> 3) % 8)) || ((h >> 7) & 1 ? ix == 0 : iy == 7);
    return stroke ? 0.92f : 0.12f;
}

static float bilinear(const float* img, int w, int h, float fx, float fy) {
    if (fx < 0) fx = 0;
    if (fy < 0) fy = 0;
    if (fx > w - 1) fx = (float)(w - 1);
    if (fy > h - 1) fy = (float)(h - 1);
    int x0 = (int)fx, y0 = (int)fy;
    int x1 = x0 + 1 < w ? x0 + 1 : x0, y1 = y0 + 1 < h ? y0 + 1 : y0;
    float ax = fx - x0, ay = fy - y0;
    return (img[y0 * w + x0] * (1 - ax) + img[y0 * w + x1] * ax) * (1 - ay)
         + (img[y1 * w + x0] * (1 - ax) + img[y1 * w + x1] * ax) * ay;
}

static void supersampleGroup(void) {
    XrCtx* c = newCtx();
    const float ratio = 2.79f;
    const int tw = (int)lroundf(SS_W / ratio), th = (int)lroundf(SS_H / ratio);
    const float phx = 0.31f, phy = 0.17f;
    float* src = malloc(sizeof(float) * SS_W * SS_H);
    uint8_t* rgba = malloc((size_t)SS_W * SS_H * 4);
    for (int y = 0; y < SS_H; y++) {
        for (int x = 0; x < SS_W; x++) {
            float v = ssSource(x, y);
            uint8_t b = (uint8_t)lroundf(v * 255.0f);
            src[y * SS_W + x] = b / 255.0f;
            uint8_t* p = rgba + ((size_t)y * SS_W + x) * 4;
            p[0] = p[1] = p[2] = b;
            p[3] = 255;
        }
    }
    M5Image pic, small;
    m5_image(&g, c, &pic, SS_W, SS_H, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
             VK_IMAGE_ASPECT_COLOR_BIT);
    m5_upload(&g, pic.img.image, SS_W, SS_H, 4, rgba);
    m5_image(&g, c, &small, tw, th, VK_FORMAT_R8G8B8A8_UNORM,
             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    // The ideal display pixels
    float* ref = malloc(sizeof(float) * tw * th);
    for (int y = 0; y < th; y++) {
        for (int x = 0; x < tw; x++) {
            float cx = (x + 0.5f + phx) * ratio, cy = (y + 0.5f + phy) * ratio;
            double sum = 0;
            for (int j = 0; j < 12; j++) {
                for (int i = 0; i < 12; i++) {
                    int sx = (int)floorf(cx + ((i + 0.5f) / 12.0f - 0.5f) * ratio);
                    int sy = (int)floorf(cy + ((j + 0.5f) / 12.0f - 0.5f) * ratio);
                    sx = sx < 0 ? 0 : sx >= SS_W ? SS_W - 1 : sx;
                    sy = sy < 0 ? 0 : sy >= SS_H ? SS_H - 1 : sy;
                    sum += src[sy * SS_W + sx];
                }
            }
            ref[y * tw + x] = (float)(sum / 144.0);
        }
    }
    // (A) the compositor's bilinear straight from the picture
    double errA[2] = { 0, 0 };
    int nReg[2] = { 0, 0 };
    for (int y = 2; y < th - 2; y++) {
        for (int x = 2; x < tw - 2; x++) {
            float v = bilinear(src, SS_W, SS_H, (x + 0.5f + phx) * ratio - 0.5f, (y + 0.5f + phy) * ratio - 0.5f);
            int reg = x >= tw / 2;
            double d = (v - ref[y * tw + x]) * 255.0;
            errA[reg] += d * d;
            nReg[reg]++;
        }
    }
    printf("supersampling, picture 2560x1440 shown %dx%d (minification %.2f), RMS error against the ideal, 0..255:\n",
           tw, th, ratio);
    printf("  (A) compositor bilinear only:              zone plate %5.1f, text %5.1f\n", sqrt(errA[0] / nReg[0]),
           sqrt(errA[1] / nReg[1]));
    double bestB[2] = { 1e9, 1e9 };
    uint8_t* out = malloc((size_t)tw * th * 4);
    float* sm = malloc(sizeof(float) * tw * th);
    int tapsList[2] = { 2, 4 };
    for (int k = 0; k < 2; k++) {
        VkCommandBuffer cmd = m5_begin(&g);
        mlxr_barrier(cmd, small.img.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        MlxrTarget t;
        memset(&t, 0, sizeof(t));
        t.view = small.img.view;
        t.format = VK_FORMAT_R8G8B8A8_UNORM;
        t.rect.extent = (VkExtent2D){ (uint32_t)tw, (uint32_t)th };
        MlxrSupersampleParams sp;
        memset(&sp, 0, sizeof(sp));
        sp.src = pic.img.view;
        sp.srcRect[2] = sp.srcRect[3] = 1.0f;
        sp.taps = tapsList[k];
        mlxr_pass_supersample(g.passes, cmd, &t, &sp);
        m5_submit(&g, cmd, NULL, 0);
        m5_readback(&g, small.img.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, tw, th, out);
        for (int i = 0; i < tw * th; i++) sm[i] = out[i * 4] / 255.0f;
        double errB[2] = { 0, 0 };
        for (int y = 2; y < th - 2; y++) {
            for (int x = 2; x < tw - 2; x++) {
                float v = bilinear(sm, tw, th, x + phx, y + phy);
                int reg = x >= tw / 2;
                double d = (v - ref[y * tw + x]) * 255.0;
                errB[reg] += d * d;
            }
        }
        printf("  (B) supersample pass, %d taps, then bilinear: zone plate %5.1f, text %5.1f\n", tapsList[k],
               sqrt(errB[0] / nReg[0]), sqrt(errB[1] / nReg[1]));
        for (int r = 0; r < 2; r++) {
            if (errB[r] < bestB[r]) bestB[r] = errB[r];
        }
    }
    CHECK(bestB[0] < errA[0] && bestB[1] < errA[1]);
    free(out);
    free(sm);
    free(ref);
    free(src);
    free(rgba);
    m5_image_free(c, &pic);
    m5_image_free(c, &small);
    xrWorldDestroy(c);
    free(c);
}

// ---- frame_models -----------------------------------------------------------------------

// A .room (MXR3) of one painted part: one triangle in the z = 0 plane facing +z, `size` metres
// across the grip and moved `shiftX` along x, in one colour; cut short by an index when asked to
static void writeTriRoom(const char* path, float size, float shiftX, const float rgb[3], int truncate) {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    uint32_t head[5] = { 0x3352584d /* 'MXR3' */, 3, 3, 1, 0 };
    fwrite(head, 4, 5, f);
    uint32_t part[3] = { 0, 3, 0xffffffffu };
    fwrite(part, 4, 3, f);
    uint32_t channels = 0;
    fwrite(&channels, 4, 1, f);
    const float pos[3][2] = { { -size, -size }, { size, -size }, { 0, size } };
    for (int i = 0; i < 3; i++) {
        float v[11] = { pos[i][0] + shiftX, pos[i][1], 0, 0, 0, 1, 0, 0, rgb[0], rgb[1], rgb[2] };
        fwrite(v, 4, 11, f);
    }
    uint32_t idx[3] = { 0, 1, 2 };
    fwrite(idx, 4, truncate ? 2 : 3, f);
    fclose(f);
}

// Native: a textured .model (MXFM 1) of one 10 cm square facing +z, moved
// shiftX along x, one component, a 4 x 4 texture of one colour (sRGB bytes)
static void writeQuadModel(const char* path, float shiftX, const uint8_t rgb[3]) {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    uint32_t head[8] = { 0x4d46584d /* 'MXFM' */, 1, 4, 6, 1, 4, 4, 0 };
    fwrite(head, 4, 8, f);
    unsigned char rec[208];
    memset(rec, 0, sizeof(rec));
    snprintf((char*)rec, 32, "body");
    uint32_t u[4] = { 0, 6, 0, 0 };
    memcpy(rec + 64, u, sizeof(u));
    fwrite(rec, 1, sizeof(rec), f);
    const float p[4][2] = { { -0.05f, -0.05f }, { 0.05f, -0.05f }, { 0.05f, 0.05f }, { -0.05f, 0.05f } };
    for (int i = 0; i < 4; i++) {
        float v[12] = { p[i][0] + shiftX, p[i][1], 0, 0, 0, 1, 1, 0, 0, -1, p[i][0] > 0 ? 1.0f : 0.0f, p[i][1] > 0 ? 0.0f : 1.0f };
        fwrite(v, 4, 12, f);
    }
    uint32_t idx[6] = { 0, 1, 2, 0, 2, 3 };
    fwrite(idx, 4, 6, f);
    for (int i = 0; i < 16; i++) {
        uint8_t c[4] = { rgb[0], rgb[1], rgb[2], 255 };
        fwrite(c, 1, 4, f);
    }
    for (int i = 0; i < 16; i++) {
        uint8_t n[4] = { 128, 128, 255, 128 };
        fwrite(n, 1, 4, f);
    }
    fclose(f);
}

static void stageAssets(const char* dir) {
    char cmd[1600];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s' && mkdir -p '%s/models' && cp '%s/models/controller.room' '%s/models/'", dir,
             dir, MLXR_ASSETS_DIR, dir);
    CHECK(system(cmd) == 0);
}

static XrCtx* ctxWithAssets(const char* dir) {
    XrCtx* c = newCtx();
    snprintf(c->assetsDir, sizeof(c->assetsDir), "%s", dir);
    c->modelOn = 1;
    m5_frame(&g, c, NULL, NULL);
    return c;
}

static void unloadWith(XrCtx* c) {
    xrWorldDestroy(c);
    free(c);
}

typedef struct {
    M5Image colour, depth;
    XrView views[2];
    int w, h;
} FrameShot;

static void frameModelBody(XrCtx* ctx, VkCommandBuffer cmd, void* user) {
    FrameShot* s = user;
    mlxr_barrier(cmd, s->colour.img.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    mlxr_barrier(cmd, s->depth.img.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    XrEyeTargets t = xrEyesSideBySide(s->colour.img.image, s->colour.img.view, VK_FORMAT_R8G8B8A8_SRGB, s->w, s->h);
    t.depthImage = s->depth.img.image;
    t.depthView = s->depth.img.view;
    xrModelRecord(ctx, cmd, &t, s->views);
}

// Mean red and blue of the drawn pixels of the left eye with only `hand` shown at the grip
static void handColour(XrCtx* c, FrameShot* s, int hand, double* red, double* blue, int* drawn, double* meanX) {
    c->modelShown[hand] = 1;
    c->modelShown[1 - hand] = 0;
    c->modelGrip[hand] = (XrPosef){ { 0, 0, 0, 1 }, { 0.0f, 0.0f, -0.5f } };
    m5_frame(&g, c, frameModelBody, s);
    uint8_t* px = malloc((size_t)2 * s->w * s->h * 4);
    m5_readback(&g, s->colour.img.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 2 * s->w, s->h, px);
    double r = 0, b = 0, sx = 0;
    int n = 0;
    for (int y = 0; y < s->h; y++) {
        for (int x = 0; x < s->w; x++) {
            const uint8_t* p = px + ((size_t)y * 2 * s->w + x) * 4;
            if (p[3] > 0) {
                r += p[0];
                b += p[2];
                sx += x;
                n++;
            }
        }
    }
    free(px);
    *red = n ? r / n : 0;
    *blue = n ? b / n : 0;
    *drawn = n;
    *meanX = n ? sx / n : 0;
}

static void frameModelsGroup(void) {
    const char* dir = "/tmp/mlxr-frame-models-test";
    char path[700];
    const float red[3] = { 1, 0, 0 }, blue[3] = { 0, 0, 1 };
    FILE* f = NULL;

    // Nothing but the generic controller: as before, one mesh for both hands
    stageAssets(dir);
    XrCtx* c = ctxWithAssets(dir);
    CHECK(c->modelReady && c->model && !c->model->perHand);
    CHECK(c->model->handFirstIndex[0] == 0 && c->model->handFirstIndex[1] == 0);
    CHECK(c->model->handIndexCount[0] == c->model->indexCount && c->model->handIndexCount[1] == c->model->indexCount);
    unloadWith(c);

    // Both hands there and valid: the pair, each hand its own range, not the generic one
    snprintf(path, sizeof(path), "%s/models/frame_controller_left.room", dir);
    writeTriRoom(path, 0.10f, 0.15f, red, 0);
    snprintf(path, sizeof(path), "%s/models/frame_controller_right.room", dir);
    writeTriRoom(path, 0.10f, 0.15f, blue, 0);
    c = ctxWithAssets(dir);
    CHECK(c->modelReady && c->model && c->model->perHand);
    CHECK(c->model->vertexCount == 6 && c->model->indexCount == 6);
    CHECK(c->model->handFirstIndex[0] == 0 && c->model->handFirstIndex[1] == 3);
    CHECK(c->model->handIndexCount[0] == 3 && c->model->handIndexCount[1] == 3);
    CHECK(c->model->handVertexOffset[0] == 0 && c->model->handVertexOffset[1] == 3);
    FrameShot s;
    memset(&s, 0, sizeof(s));
    s.w = 256;
    s.h = 256;
    m5_image(&g, c, &s.colour, 2 * s.w, s.h, VK_FORMAT_R8G8B8A8_SRGB,
             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    m5_image(&g, c, &s.depth, 2 * s.w, s.h, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
             VK_IMAGE_ASPECT_DEPTH_BIT);
    s.views[0] = m5_view(60.0f, s.w, s.h, 0.0f);
    s.views[1] = s.views[0];
    for (int e = 0; e < 2; e++) c->modelViews[e] = s.views[e];
    double r, b, xl, xr;
    int n;
    handColour(c, &s, HAND_LEFT, &r, &b, &n, &xl);
    printf("  left hand: %d px, red %.0f blue %.0f, mean x %.1f of %d\n", n, r, b, xl, s.w);
    CHECK(n > 1000 && r > 50 && b < 5);
    handColour(c, &s, HAND_RIGHT, &r, &b, &n, &xr);
    printf("  right hand: %d px, red %.0f blue %.0f, mean x %.1f of %d\n", n, r, b, xr, s.w);
    CHECK(n > 1000 && b > 50 && r < 5);
    // Not mirrored: both triangles were made 0.15 m to the right and both are drawn there (the
    // generic left would be flipped to the left of the grip)
    CHECK(xl > s.w / 2 + 20 && xr > s.w / 2 + 20);
    m5_image_free(c, &s.colour);
    m5_image_free(c, &s.depth);
    unloadWith(c);

    // Native: the textured pair beside the painted one: the textured one,
    // drawn through xrModelRecord (MLXR_PASS_MODEL_TEX), each hand its own,
    // not mirrored, a red and a blue texture coming out red and blue
    const uint8_t texRed[3] = { 220, 20, 20 }, texBlue[3] = { 20, 20, 220 };
    snprintf(path, sizeof(path), "%s/models/frame_controller_left.model", dir);
    writeQuadModel(path, 0.15f, texRed);
    snprintf(path, sizeof(path), "%s/models/frame_controller_right.model", dir);
    writeQuadModel(path, 0.15f, texBlue);
    c = ctxWithAssets(dir);
    CHECK(c->modelReady && c->model && c->model->perHand && c->model->textured);
    CHECK(c->model->vertexCount == 8 && c->model->indexCount == 12 && c->model->handFirstIndex[1] == 6);
    memset(&s, 0, sizeof(s));
    s.w = 256;
    s.h = 256;
    m5_image(&g, c, &s.colour, 2 * s.w, s.h, VK_FORMAT_R8G8B8A8_SRGB,
             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    m5_image(&g, c, &s.depth, 2 * s.w, s.h, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
             VK_IMAGE_ASPECT_DEPTH_BIT);
    s.views[0] = m5_view(60.0f, s.w, s.h, 0.0f);
    s.views[1] = s.views[0];
    for (int e = 0; e < 2; e++) c->modelViews[e] = s.views[e];
    handColour(c, &s, HAND_LEFT, &r, &b, &n, &xl);
    printf("  textured left hand: %d px, red %.0f blue %.0f, mean x %.1f of %d\n", n, r, b, xl, s.w);
    CHECK(n > 1000 && r > b + 5);
    handColour(c, &s, HAND_RIGHT, &r, &b, &n, &xr);
    printf("  textured right hand: %d px, red %.0f blue %.0f, mean x %.1f of %d\n", n, r, b, xr, s.w);
    CHECK(n > 1000 && b > r + 5);
    CHECK(xl > s.w / 2 + 20 && xr > s.w / 2 + 20);
    m5_image_free(c, &s.colour);
    m5_image_free(c, &s.depth);
    unloadWith(c);
    // A textured half that does not parse: the painted pair again
    snprintf(path, sizeof(path), "%s/models/frame_controller_right.model", dir);
    f = fopen(path, "wb");
    fputs("not a model", f);
    fclose(f);
    c = ctxWithAssets(dir);
    CHECK(c->modelReady && c->model && c->model->perHand && !c->model->textured);
    unloadWith(c);
    remove(path);
    snprintf(path, sizeof(path), "%s/models/frame_controller_left.model", dir);
    remove(path);

    // One hand missing: the generic one, both hands
    snprintf(path, sizeof(path), "%s/models/frame_controller_left.room", dir);
    remove(path);
    c = ctxWithAssets(dir);
    CHECK(c->modelReady && c->model && !c->model->perHand);
    unloadWith(c);

    // One hand cut short (a file that does not parse), or not a mesh at all: the generic one
    writeTriRoom(path, 0.10f, 0.15f, red, 1);
    c = ctxWithAssets(dir);
    CHECK(c->modelReady && c->model && !c->model->perHand);
    unloadWith(c);
    writeTriRoom(path, 0.10f, 0.15f, red, 0);
    snprintf(path, sizeof(path), "%s/models/frame_controller_right.room", dir);
    f = fopen(path, "wb");
    fputs("this is not a mesh", f);
    fclose(f);
    c = ctxWithAssets(dir);
    CHECK(c->modelReady && c->model && !c->model->perHand);
    unloadWith(c);

    // Corrupt pair and no generic one either: no model, and it is said so (not a crash)
    snprintf(path, sizeof(path), "%s/models/controller.room", dir);
    remove(path);
    c = ctxWithAssets(dir);
    CHECK(!c->modelReady && c->modelLoadFailed);
    unloadWith(c);
}

// ---- native (lane eye-swapchains): an image per eye ---------------------------------------
// The session draws every projection layer into a swapchain per eye (SteamVR
// downsamples a texture past 3458 px, and two 2160 eyes side by side are 4320).
// Each pass drawn that way must give the very pixels the side by side draw
// gave each half, and cost no more.

static void shotMake(XrCtx* c, RoomShot* r, int w, int h, int perEye) {
    memset(r, 0, sizeof(*r));
    r->eyeW = w;
    r->eyeH = h;
    r->perEye = perEye;
    VkImageUsageFlags u = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (perEye) {
        for (int e = 0; e < 2; e++) m5_image(&g, c, &r->eye[e], w, h, VK_FORMAT_R8G8B8A8_SRGB, u, VK_IMAGE_ASPECT_COLOR_BIT);
    }
    else {
        m5_image(&g, c, &r->colour, 2 * w, h, VK_FORMAT_R8G8B8A8_SRGB, u, VK_IMAGE_ASPECT_COLOR_BIT);
    }
    m5_image(&g, c, &r->depth, perEye ? w : 2 * w, h, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
             VK_IMAGE_ASPECT_DEPTH_BIT);
    r->views[0] = m5_view(100.0f, w, h, -0.032f);
    r->views[1] = m5_view(100.0f, w, h, 0.032f);
}

static void shotFree(XrCtx* c, RoomShot* r) {
    m5_image_free(c, &r->colour);
    m5_image_free(c, &r->eye[0]);
    m5_image_free(c, &r->eye[1]);
    m5_image_free(c, &r->depth);
}

static void runOnce(XrCtx* c, PassFn fn, void* user) {
    VkCommandBuffer cmd = m5_begin(&g);
    c->frameCmd = cmd;
    fn(c, cmd, user);
    c->frameCmd = VK_NULL_HANDLE;
    m5_submit(&g, cmd, NULL, 0);
}

// Each eye of the per eye shot against its half of the side by side one: the
// largest channel difference, the pixels drawn (alpha or colour above the
// clear) and the largest difference between an eye and the other half
static void compareEyes(RoomShot* side, RoomShot* per, int* maxDiff, long* drawn, int* crossDiff) {
    int w = side->eyeW, h = side->eyeH;
    uint8_t* a = malloc((size_t)2 * w * h * 4);
    uint8_t* b = malloc((size_t)w * h * 4);
    m5_readback(&g, side->colour.img.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 2 * w, h, a);
    *maxDiff = 0;
    *drawn = 0;
    *crossDiff = 0;
    for (int e = 0; e < 2; e++) {
        m5_readback(&g, per->eye[e].img.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, w, h, b);
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                const uint8_t* p = b + ((size_t)y * w + x) * 4;
                const uint8_t* q = a + ((size_t)y * 2 * w + e * w + x) * 4;
                const uint8_t* o = a + ((size_t)y * 2 * w + (1 - e) * w + x) * 4;
                if (p[3] > 8 || p[0] + p[1] + p[2] > 24) (*drawn)++;
                for (int k = 0; k < 4; k++) {
                    int d = abs(p[k] - q[k]), od = abs(p[k] - o[k]);
                    if (d > *maxDiff) *maxDiff = d;
                    if (od > *crossDiff) *crossDiff = od;
                }
            }
        }
    }
    free(a);
    free(b);
}

static void eyeImagesGroup(void) {
    XrCtx* c = newCtx();
    m5_frame(&g, c, NULL, NULL);
    m5_video(&g, c, &video, 1920, 1080, twoColour);
    c->ambilightOn = 1;
    c->ambiIntensity = 0.6f;
    for (int f = 0; f < 3; f++) m5_frame(&g, c, glowBody, NULL);
    c->ambiSeeded = 1;
    c->roomLightOn = 1;
    xrSetEnvironment(c, roomCellForStyle(ROOM_STYLE_THEATER));
    untilBuilt(c, ROOM_STYLE_THEATER, NULL);
    c->modelOn = 1;
    m5_frame(&g, c, NULL, NULL);
    c->modelShown[0] = c->modelShown[1] = 1;
    Vec3 ax = { 1, 0, 0 };
    c->modelGrip[0] = (XrPosef){ axisAngleQuat(ax, -0.5f), { -0.18f, -0.22f, -0.45f } };
    c->modelGrip[1] = (XrPosef){ axisAngleQuat(ax, -0.5f), { 0.20f, -0.20f, -0.50f } };
    WarpShot pic;
    memset(&pic, 0, sizeof(pic));
    pic.w = 1920;
    pic.h = 1080;
    m5_image(&g, c, &pic.target, 1920, 1080, VK_FORMAT_R8G8B8A8_UNORM,
             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    {
        VkCommandBuffer cmd = m5_begin(&g);
        c->frameCmd = cmd;
        warpBody(c, cmd, &pic);
        mlxr_barrier(cmd, pic.target.img.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        c->frameCmd = VK_NULL_HANDLE;
        m5_submit(&g, cmd, NULL, 0);
    }

    // The same pixels, an eye at a time into its own image
    const char* names[3] = { "room (home theater)", "controller models", "curved picture + glow" };
    PassFn fns[3] = { drawRoomBody, modelsBody, curveBody };
    RoomShot side, per;
    shotMake(c, &side, 480, 432, 0);
    shotMake(c, &per, 480, 432, 1);
    printf("an image per eye against the halves of one image (480x432 an eye):\n");
    for (int k = 0; k < 3; k++) {
        CurveShot cs[2] = { { &side, pic.target.img.view }, { &per, pic.target.img.view } };
        runOnce(c, fns[k], k == 2 ? (void*)&cs[0] : (void*)&side);
        runOnce(c, fns[k], k == 2 ? (void*)&cs[1] : (void*)&per);
        int maxDiff, crossDiff;
        long drawn;
        compareEyes(&side, &per, &maxDiff, &drawn, &crossDiff);
        printf("  %-24s max diff %d, %ld pixels drawn, the other eye's half differs by up to %d\n", names[k],
               maxDiff, drawn, crossDiff);
        CHECK(maxDiff <= 2);
        CHECK(drawn > 1000);
        // each eye drawn from its own view, not the other's
        CHECK(crossDiff > 16);
    }
    shotFree(c, &side);
    shotFree(c, &per);

    // The cost at the Frame's 2160 an eye: each eye drawn once either way
    Timer t;
    VkQueryPoolCreateInfo qi = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
    qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qi.queryCount = 2;
    vkCreateQueryPool(g.vk.device, &qi, NULL, &t.q);
    t.tickNs = g.props.limits.timestampPeriod;
    shotMake(c, &side, 2160, 2160, 0);
    shotMake(c, &per, 2160, 2160, 1);
    printf("GPU time at 2160x2160 an eye, median of 25 (side by side 4320 wide / an image per eye):\n");
    for (int k = 0; k < 3; k++) {
        CurveShot cs[2] = { { &side, pic.target.img.view }, { &per, pic.target.img.view } };
        double a = timePass(c, &t, fns[k], k == 2 ? (void*)&cs[0] : (void*)&side);
        double b = timePass(c, &t, fns[k], k == 2 ? (void*)&cs[1] : (void*)&per);
        printf("  %-24s %.3f ms / %.3f ms\n", names[k], a, b);
        CHECK_BUDGET(b <= a * 1.3 + 0.3);
    }
    shotFree(c, &side);
    shotFree(c, &per);
    vkDestroyQueryPool(g.vk.device, t.q, NULL);
    m5_image_free(c, &pic.target);
    m5_video_free(c, &video);
    xrWorldDestroy(c);
    free(c);
}

int main(int argc, char** argv) {
    const char* grp = argc > 1 ? argv[1] : "all";
    int all = !strcmp(grp, "all");
    int gpu = m5_gpu_init(&g);
    if (gpu != 0) {
        fprintf(stderr, "no Vulkan device\n");
        return gpu == M5_GPU_NO_DEVICE ? 77 : 1;
    }
    // The rooms' atlases are ASTC: on a device without textureCompressionASTC_LDR
    // (lavapipe) no room loads, so the groups that draw one skip there
    int skipped = 0;
    if (all || !strcmp(grp, "leaks")) {
        if (g.astc) leaksGroup();
        else skipped++;
    }
    if (all || !strcmp(grp, "timing")) {
        if (g.astc) timingGroup();
        else skipped++;
    }
    if (all || !strcmp(grp, "glow")) glowGroup();
    if (all || !strcmp(grp, "curve")) curveGroup();
    if (all || !strcmp(grp, "supersample")) supersampleGroup();
    if (all || !strcmp(grp, "frame_models")) frameModelsGroup();
    if (all || !strcmp(grp, "eye_images")) {
        if (g.astc) eyeImagesGroup();
        else skipped++;
    }
    m5_gpu_free(&g);
    if (skipped) printf("%d group(s) skipped: no textureCompressionASTC_LDR for the rooms' atlases\n", skipped);
    if (skipped && !all) return 77;
    char name[32];
    snprintf(name, sizeof(name), "m5 world %s", grp);
    return checksDone(name);
}
