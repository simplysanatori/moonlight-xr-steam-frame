// Moonlight XR Native: the GPU side the room, the glow and the controller
// models share (see xr_world.h). Nothing here is upstream's: it is what GL did
// for upstream behind its calls (object lifetimes, ordering, the swapchain
// images' release) and the GPU timer, which upstream had as GL timer queries
// (natively timestamps around each section of the frame). GPLv3.
#include "xr_world.h"

#include "xr_native.h"

#ifndef MLXR_ASSETS_DIR
#define MLXR_ASSETS_DIR "assets"
#endif

#define UPLOADS_MAX 8
#define RETIRED_MAX 8

typedef struct {
    VkCommandPool pool;    // may be NULL: recorded into the frame's own buffer
    VkCommandBuffer cmd;
    MlxrRoom* room;        // whose staging goes once it is done
    MlxrModel* model;
    uint64_t serial;       // 0 until submitted
} Upload;

typedef struct XrWorld {
    Upload uploads[UPLOADS_MAX];
    int uploadCount;
    struct {
        MlxrRoom* room;
        uint64_t serial;
    } retired[RETIRED_MAX];
    int retiredCount;
    int retiredTotal;
    // GPU time
    VkQueryPool queries;
    int queriesFailed;
    double tickNs;
    unsigned written;      // sections begun and ended in the frame recorded last
    unsigned writing;      // this frame's
    int begun[GPU_SECTIONS];
    double sumMs[GPU_SECTIONS];
    double maxMs[GPU_SECTIONS];
    int samples[GPU_SECTIONS];
} XrWorld;

static XrWorld* world(XrCtx* ctx) {
    if (ctx->world == NULL) {
        ctx->world = calloc(1, sizeof(XrWorld));
    }
    return ctx->world;
}

// ---- small helpers -------------------------------------------------------------------

MlxrGpu xrWorldGpu(XrCtx* ctx) {
    MlxrGpu g;
    g.phys = ctx->vk->phys;
    g.device = ctx->vk->device;
    g.queueFamily = ctx->vk->queueFamily;
    g.queue = ctx->vk->queue;
    return g;
}

int xrVkImageMake(XrCtx* ctx, XrVkImage* img, int w, int h, VkFormat format, VkImageUsageFlags usage,
                  VkImageAspectFlags aspect) {
    const XrVk* vk = ctx->vk;
    memset(img, 0, sizeof(*img));
    VkImageCreateInfo ci = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = format;
    ci.extent = (VkExtent3D){ (uint32_t)w, (uint32_t)h, 1 };
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = usage;
    if (vkCreateImage(vk->device, &ci, NULL, &img->image) != VK_SUCCESS) {
        return 0;
    }
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(vk->device, img->image, &req);
    VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = xr_vk_memory_type(vk, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(vk->device, &ai, NULL, &img->memory) != VK_SUCCESS) {
        xrVkImageFree(ctx, img);
        return 0;
    }
    vkBindImageMemory(vk->device, img->image, img->memory, 0);
    VkImageViewCreateInfo vi = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    vi.image = img->image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    vi.subresourceRange = (VkImageSubresourceRange){ aspect, 0, 1, 0, 1 };
    if (vkCreateImageView(vk->device, &vi, NULL, &img->view) != VK_SUCCESS) {
        xrVkImageFree(ctx, img);
        return 0;
    }
    img->format = format;
    img->layout = VK_IMAGE_LAYOUT_UNDEFINED;
    img->width = w;
    img->height = h;
    return 1;
}

void xrVkImageFree(XrCtx* ctx, XrVkImage* img) {
    VkDevice d = ctx->vk->device;
    if (img->view) vkDestroyImageView(d, img->view, NULL);
    if (img->image) vkDestroyImage(d, img->image, NULL);
    if (img->memory) vkFreeMemory(d, img->memory, NULL);
    memset(img, 0, sizeof(*img));
}

void xrVkImageTo(XrCtx* ctx, XrVkImage* img, VkImageAspectFlags aspect, VkImageLayout to) {
    mlxr_barrier(ctx->frameCmd, img->image, aspect, img->layout, to);
    img->layout = to;
}

int createArtSwapchain(XrCtx* ctx, int width, int height, const char* what, XrSwapchain* out,
                       XrSwapchainImageVulkan2KHR** images, VkImageView** views, uint32_t* count) {
    XrSwapchainCreateInfo ci = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
    ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT
            | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    ci.format = VK_FORMAT_R8G8B8A8_SRGB;
    ci.sampleCount = 1;
    ci.width = (uint32_t)width;
    ci.height = (uint32_t)height;
    ci.faceCount = ci.arraySize = ci.mipCount = 1;
    XrSwapchain sc = XR_NULL_HANDLE;
    if (!checkXr(xrCreateSwapchain(ctx->session, &ci, &sc), what)) {
        return 0;
    }
    ctx->swapchainsAlive++;
    uint32_t n = 0;
    xrEnumerateSwapchainImages(sc, 0, &n, NULL);
    XrSwapchainImageVulkan2KHR* imgs = calloc(n ? n : 1, sizeof(*imgs));
    VkImageView* vs = calloc(n ? n : 1, sizeof(*vs));
    for (uint32_t i = 0; i < n; i++) imgs[i].type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR;
    xrEnumerateSwapchainImages(sc, n, &n, (XrSwapchainImageBaseHeader*)imgs);
    for (uint32_t i = 0; i < n; i++) {
        VkImageViewCreateInfo vi = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        vi.image = imgs[i].image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = VK_FORMAT_R8G8B8A8_SRGB;
        vi.subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCreateImageView(ctx->vk->device, &vi, NULL, &vs[i]);
    }
    *out = sc;
    *images = imgs;
    *views = vs;
    *count = n;
    return 1;
}

void destroyArtSwapchain(XrCtx* ctx, XrSwapchain* swapchain, XrSwapchainImageVulkan2KHR** images,
                         VkImageView** views, uint32_t* count) {
    if (views != NULL && *views != NULL) {
        for (uint32_t i = 0; i < *count; i++) {
            if ((*views)[i]) vkDestroyImageView(ctx->vk->device, (*views)[i], NULL);
        }
        free(*views);
        *views = NULL;
    }
    free(*images);
    *images = NULL;
    *count = 0;
    if (*swapchain != XR_NULL_HANDLE) {
        xrDestroySwapchain(*swapchain);
        ctx->swapchainsAlive--;
        *swapchain = XR_NULL_HANDLE;
    }
}

int xrArtAcquire(XrSwapchain swapchain, const char* what, uint32_t* index) {
    XrSwapchainImageAcquireInfo aq = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    if (!checkXr(xrAcquireSwapchainImage(swapchain, &aq, index), what)) {
        return 0;
    }
    XrSwapchainImageWaitInfo wi = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    wi.timeout = XR_INFINITE_DURATION;
    if (!checkXr(xrWaitSwapchainImage(swapchain, &wi), what)) {
        XrSwapchainImageReleaseInfo rl = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
        xrReleaseSwapchainImage(swapchain, &rl);
        return 0;
    }
    return 1;
}

VkImageView xrWorldDepth(XrCtx* ctx, int w, int h) {
    XrVkImage* d = &ctx->worldDepth;
    if (d->image == VK_NULL_HANDLE || d->width < w || d->height < h) {
        // Only ever made larger, between frames the GPU has finished
        if (d->image != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(ctx->vk->device);
            xrVkImageFree(ctx, d);
        }
        if (!xrVkImageMake(ctx, d, w, h, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                           VK_IMAGE_ASPECT_DEPTH_BIT)) {
            LOGE("room depth %dx%d could not be made", w, h);
            return VK_NULL_HANDLE;
        }
        LOGEV("room and model depth %dx%d, %.1f MB", w, h, w * (double)h * 4.0 / (1024.0 * 1024.0));
    }
    // DEPTH -> DEPTH orders it after the pass before (the room's, then the
    // models' in the same command buffer, both clearing it)
    mlxr_barrier(ctx->frameCmd, d->image, VK_IMAGE_ASPECT_DEPTH_BIT, d->layout,
                 VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    d->layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    return d->view;
}

// ---- uploads and retirement ------------------------------------------------------------

// Native, called by the room's loader hand off and the model's load
void xrWorldQueueUpload(XrCtx* ctx, VkCommandPool pool, VkCommandBuffer cmd, MlxrRoom* room, MlxrModel* model);
void xrWorldRetireRoom(XrCtx* ctx, MlxrRoom* room, uint64_t lastUse);

void xrWorldQueueUpload(XrCtx* ctx, VkCommandPool pool, VkCommandBuffer cmd, MlxrRoom* room, MlxrModel* model) {
    XrWorld* w = world(ctx);
    MlxrGpu g = xrWorldGpu(ctx);
    if (w->uploadCount >= UPLOADS_MAX) {
        // Never seen: a dozen loads in flight at once. Done the slow way.
        LOGW("uploads backed up, waiting for the GPU");
        vkDeviceWaitIdle(ctx->vk->device);
        if (room) mlxr_room_upload_done(&g, room);
        if (model) mlxr_model_upload_done(&g, model);
        if (pool) vkDestroyCommandPool(ctx->vk->device, pool, NULL);
        return;
    }
    Upload* u = &w->uploads[w->uploadCount++];
    memset(u, 0, sizeof(*u));
    u->pool = pool;
    u->cmd = cmd;
    u->room = room;
    u->model = model;
    // Recorded into the frame's own buffer: in this frame's submission
    if (pool == VK_NULL_HANDLE) {
        u->serial = ctx->gpuSerial;
    }
}

void xrWorldRetireRoom(XrCtx* ctx, MlxrRoom* room, uint64_t lastUse) {
    XrWorld* w = world(ctx);
    if (w->retiredCount >= RETIRED_MAX) {
        vkDeviceWaitIdle(ctx->vk->device);
        MlxrGpu g = xrWorldGpu(ctx);
        mlxr_room_unload(&g, room);
        w->retiredTotal++;
        return;
    }
    w->retired[w->retiredCount].room = room;
    w->retired[w->retiredCount].serial = lastUse;
    w->retiredCount++;
}

int xrWorldTakeUploads(XrCtx* ctx, VkCommandBuffer* out, int max) {
    XrWorld* w = world(ctx);
    int n = 0;
    for (int i = 0; i < w->uploadCount && n < max; i++) {
        if (w->uploads[i].serial == 0 && w->uploads[i].pool != VK_NULL_HANDLE) {
            out[n++] = w->uploads[i].cmd;
        }
    }
    return n;
}

void xrWorldSubmitted(XrCtx* ctx, uint64_t serial) {
    XrWorld* w = world(ctx);
    for (int i = 0; i < w->uploadCount; i++) {
        if (w->uploads[i].serial == 0) {
            w->uploads[i].serial = serial;
        }
    }
    w->written = w->writing;
    w->writing = 0;
}

// What the GPU is done with: staging, loader pools, rooms retired
static void collect(XrCtx* ctx, int all) {
    XrWorld* w = world(ctx);
    MlxrGpu g = xrWorldGpu(ctx);
    int keep = 0;
    for (int i = 0; i < w->uploadCount; i++) {
        Upload* u = &w->uploads[i];
        if (all || (u->serial != 0 && u->serial <= ctx->gpuDone)) {
            if (u->room) mlxr_room_upload_done(&g, u->room);
            if (u->model) mlxr_model_upload_done(&g, u->model);
            if (u->pool) vkDestroyCommandPool(ctx->vk->device, u->pool, NULL);
            continue;
        }
        w->uploads[keep++] = *u;
    }
    w->uploadCount = keep;
    keep = 0;
    for (int i = 0; i < w->retiredCount; i++) {
        if (all || w->retired[i].serial <= ctx->gpuDone) {
            MlxrRoom* r = w->retired[i].room;
            LOGI("room style %d unloaded (%.1f MB back)", r->style, r->gpuBytes / (1024.0 * 1024.0));
            mlxr_room_unload(&g, r);
            w->retiredTotal++;
            continue;
        }
        w->retired[keep++] = w->retired[i];
    }
    w->retiredCount = keep;
}

int xrRoomsRetired(XrCtx* ctx) {
    XrWorld* w = world(ctx);
    return w->retiredTotal * 100 + w->retiredCount;
}

// ---- GPU time ------------------------------------------------------------------------------

static void timesCollect(XrCtx* ctx) {
    XrWorld* w = world(ctx);
    if (w->queries == VK_NULL_HANDLE) {
        if (w->queriesFailed) return;
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(ctx->vk->phys, &props);
        VkQueryPoolCreateInfo qi = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
        qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
        qi.queryCount = 2 * GPU_SECTIONS;
        if (props.limits.timestampPeriod <= 0.0f
                || vkCreateQueryPool(ctx->vk->device, &qi, NULL, &w->queries) != VK_SUCCESS) {
            w->queriesFailed = 1;
            LOGW("GPU timestamps unavailable, no GPU times in the stats line");
            return;
        }
        w->tickNs = props.limits.timestampPeriod;
        w->written = 0;
    }
    if (w->written) {
        uint64_t ticks[2 * GPU_SECTIONS];
        if (vkGetQueryPoolResults(ctx->vk->device, w->queries, 0, 2 * GPU_SECTIONS, sizeof(ticks), ticks,
                                  sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) {
            for (int s = 0; s < GPU_SECTIONS; s++) {
                if (!(w->written & (1u << s)) || ticks[2 * s + 1] < ticks[2 * s]) continue;
                double ms = (double)(ticks[2 * s + 1] - ticks[2 * s]) * w->tickNs / 1e6;
                if (ms > 50.0) continue; // upstream's plausibility filter
                w->sumMs[s] += ms;
                if (ms > w->maxMs[s]) w->maxMs[s] = ms;
                w->samples[s]++;
            }
        }
        w->written = 0;
    }
    vkCmdResetQueryPool(ctx->frameCmd, w->queries, 0, 2 * GPU_SECTIONS);
    w->writing = 0;
    memset(w->begun, 0, sizeof(w->begun));
}

void xrGpuTimeBegin(XrCtx* ctx, int section) {
    XrWorld* w = world(ctx);
    if (w->queries == VK_NULL_HANDLE || ctx->frameCmd == VK_NULL_HANDLE || w->begun[section]) return;
    w->begun[section] = 1;
    vkCmdWriteTimestamp(ctx->frameCmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, w->queries, 2 * section);
}

void xrGpuTimeEnd(XrCtx* ctx, int section) {
    XrWorld* w = world(ctx);
    if (w->queries == VK_NULL_HANDLE || ctx->frameCmd == VK_NULL_HANDLE || w->begun[section] != 1) return;
    w->begun[section] = 2;
    vkCmdWriteTimestamp(ctx->frameCmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, w->queries, 2 * section + 1);
    w->writing |= 1u << section;
}

void xrGpuTimesLine(XrCtx* ctx, char* buf, int len) {
    static const char* const NAMES[GPU_SECTIONS] = { "warp", "glow", "room", "models", "curve" };
    XrWorld* w = world(ctx);
    int n = 0;
    buf[0] = '\0';
    for (int s = 0; s < GPU_SECTIONS && n < len - 1; s++) {
        if (w->samples[s] == 0) continue;
        n += snprintf(buf + n, (size_t)(len - n), "%s%s %.2f ms (max %.2f, %d)", n ? ", " : "gpu ", NAMES[s],
                      w->sumMs[s] / w->samples[s], w->maxMs[s], w->samples[s]);
    }
    memset(w->sumMs, 0, sizeof(w->sumMs));
    memset(w->maxMs, 0, sizeof(w->maxMs));
    memset(w->samples, 0, sizeof(w->samples));
}

// ---- the frame ----------------------------------------------------------------------------

void xrWorldFindAssets(XrCtx* ctx) {
    char exe[400];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    char beside[460] = "";
    if (n > 0) {
        exe[n] = '\0';
        char* slash = strrchr(exe, '/');
        if (slash) *slash = '\0';
        snprintf(beside, sizeof(beside), "%s/assets", exe);
    }
    const char* tries[3] = { "assets", beside, MLXR_ASSETS_DIR };
    for (int i = 0; i < 3; i++) {
        char probe[600];
        snprintf(probe, sizeof(probe), "%s/rooms/home_theater.room", tries[i]);
        if (tries[i][0] && access(probe, R_OK) == 0) {
            snprintf(ctx->assetsDir, sizeof(ctx->assetsDir), "%s", tries[i]);
            LOGEV("rooms and controller model read from %s", ctx->assetsDir);
            return;
        }
    }
    snprintf(ctx->assetsDir, sizeof(ctx->assetsDir), "%s", MLXR_ASSETS_DIR);
    LOGW("no rooms found under ./assets or %s: the rooms will stay dark", MLXR_ASSETS_DIR);
}

// Upstream: nativeUploadControllerModel, from the frame loop. Read off the
// assets the first frame the switch is on, into the frame's own buffer.
static void loadControllerModel(XrCtx* ctx) {
    MlxrGpu g = xrWorldGpu(ctx);
    MlxrModel* m = NULL;
    if (mlxr_model_record(&g, ctx->assetsDir, ctx->frameCmd, &m) != 0) {
        ctx->modelLoadFailed = 1;
        LOGW("controller model is not a mesh the renderer reads (%s/models/controller.room)", ctx->assetsDir);
        return;
    }
    xrWorldQueueUpload(ctx, VK_NULL_HANDLE, VK_NULL_HANDLE, NULL, m);
    ctx->model = m;
    ctx->modelReady = 1;
    LOGEV("controller model ready: %u vertices, %u triangles, %.0f KB, %.1f ms, %s (the generic controller: "
          "no Steam Frame model ships)", m->vertexCount, m->indexCount / 3, m->gpuBytes / 1024.0, m->loadMs,
          ctx->modelOn ? "shown" : "off for now");
}

void xrWorldFrameBegin(XrCtx* ctx) {
    collect(ctx, 0);
    timesCollect(ctx);
    if (!ctx->ambiReady && !ctx->ambiFailed) {
        initAmbilight(ctx);
    }
    if (ctx->modelOn && !ctx->modelReady && !ctx->modelLoadFailed) {
        loadControllerModel(ctx);
    }
    if (roomEffective(ctx) > 0) {
        prepareRoom(ctx);
    }
}

void xrWorldReleaseImages(XrCtx* ctx) {
    XrSwapchainImageReleaseInfo rl = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    if (ctx->roomAcquired) {
        checkXr(xrReleaseSwapchainImage(ctx->roomSwapchain, &rl), "release room image");
        ctx->roomAcquired = 0;
    }
    if (ctx->modelAcquired) {
        checkXr(xrReleaseSwapchainImage(ctx->modelSwapchain, &rl), "release controller model image");
        ctx->modelAcquired = 0;
    }
    if (ctx->glowAcquired) {
        checkXr(xrReleaseSwapchainImage(ctx->glowSwapchain, &rl), "release glow image");
        ctx->glowAcquired = 0;
        ctx->glowSwapchainFilled = 1;
    }
}

// Native: the room's loader thread and resident room (xr_room.c)
void xrRoomDestroy(XrCtx* ctx);

void xrWorldDestroy(XrCtx* ctx) {
    if (ctx->vk == NULL || ctx->vk->device == VK_NULL_HANDLE) {
        return;
    }
    vkDeviceWaitIdle(ctx->vk->device);
    xrRoomDestroy(ctx);
    collect(ctx, 1);
    MlxrGpu g = xrWorldGpu(ctx);
    if (ctx->model) {
        mlxr_model_unload(&g, ctx->model);
        ctx->model = NULL;
    }
    ctx->modelReady = 0;
    destroyArtSwapchain(ctx, &ctx->roomSwapchain, &ctx->roomImages, &ctx->roomImageViews, &ctx->roomImageCount);
    destroyArtSwapchain(ctx, &ctx->modelSwapchain, &ctx->modelImages, &ctx->modelImageViews,
                        &ctx->modelImageCount);
    destroyArtSwapchain(ctx, &ctx->glowSwapchain, &ctx->glowImages, NULL, &ctx->glowImageCount);
    xrVkImageFree(ctx, &ctx->worldDepth);
    xrVkImageFree(ctx, &ctx->ambiTexture);
    xrVkImageFree(ctx, &ctx->ambiDetectTexture);
    xrVkImageFree(ctx, &ctx->glowEdgeTexture);
    xrVkImageFree(ctx, &ctx->glowTexture);
    if (ctx->ambiDetectBuffer) vkDestroyBuffer(ctx->vk->device, ctx->ambiDetectBuffer, NULL);
    if (ctx->ambiDetectMemory) vkFreeMemory(ctx->vk->device, ctx->ambiDetectMemory, NULL);
    ctx->ambiDetectBuffer = VK_NULL_HANDLE;
    ctx->ambiDetectMemory = VK_NULL_HANDLE;
    ctx->ambiReady = 0;
    ctx->roomReady = ctx->modelPassReady = 0;
    XrWorld* w = ctx->world;
    if (w != NULL) {
        if (w->queries) vkDestroyQueryPool(ctx->vk->device, w->queries, NULL);
        free(w);
        ctx->world = NULL;
    }
}

// ---- the picture's colours and the glow (upstream renderVideoFrame's part) -----------------

void xrWorldVideoFrame(XrCtx* ctx, const MlxrVideo* video) {
    if (!ctx->ambiReady || ctx->frameCmd == VK_NULL_HANDLE) {
        return;
    }
    int roomOn = roomEffective(ctx) > 0;
    int glowOn;
    float glowLevel;
    ambiEffective(ctx, &glowOn, &glowLevel);
    // The room is lit from the same colours the glow is made of, so the
    // sample is taken for either of them
    int sampled = glowOn || roomOn;
    xrGpuTimeBegin(ctx, GPU_GLOW);
    if (!sampled) {
        // Kept from whatever picture was up when it last ran, so the next
        // sample lands whole rather than easing in from that one
        ctx->ambiSeeded = 0;
    }
    if (sampled) {
        runFrameColorSample(ctx, video);
    }
    if (glowOn) {
        runGlowRender(ctx);
    }
    else {
        ctx->glowDrawnOn = 0;
    }
    // Letterbox detection, once every so many frames the colour sample ran:
    // last time's readback first (its frame has finished), then this frame's
    // request if it is due
    if (sampled && ctx->ambiBarDetect) {
        finishAmbiBarDetect(ctx);
        ctx->ambiBarCounter++;
        if (ctx->ambiBarCounter >= AMBI_BAR_PERIOD) {
            ctx->ambiBarCounter = 0;
            runAmbiBarDetect(ctx, video);
        }
    }
    xrGpuTimeEnd(ctx, GPU_GLOW);
}

void xrWorldStillFrame(XrCtx* ctx, const MlxrVideo* video) {
    if (!ctx->ambiReady || ctx->frameCmd == VK_NULL_HANDLE || !glowStale(ctx)) {
        return;
    }
    // The glow's level or switch moved with nothing new from the decoder
    xrGpuTimeBegin(ctx, GPU_GLOW);
    redrawGlow(ctx, video);
    xrGpuTimeEnd(ctx, GPU_GLOW);
    LOGI("glow redrawn on a still picture at %.2f", ctx->glowDrawnLevel);
}

void xrWorldVideoGone(XrCtx* ctx) {
    ctx->ambiSeeded = 0;
    ctx->glowRendered = 0;
    ctx->glowDrawnOn = 0;
    ctx->ambiDetectPending = 0;
}

int xrWorldGlowShown(XrCtx* ctx) {
    int on;
    float level;
    ambiEffective(ctx, &on, &level);
    // Native: the launcher's list stands in the picture's place with no
    // picture behind it to glow, and a stream that stopped leaves none
    return on && ctx->glowRendered && ctx->everRendered && ctx->shouldRender && ctx->videoShown
            && ctx->launcher == LAUNCHER_NONE;
}

void xrWorldFlatGlow(XrCtx* ctx, int curved) {
    if (curved || !xrWorldGlowShown(ctx) || ctx->glowCopied || ctx->frameCmd == VK_NULL_HANDLE || ctx->offscreen) {
        return;
    }
    if (ctx->glowSwapchain == XR_NULL_HANDLE) {
        if (ctx->glowFailed) {
            return;
        }
        VkImageView* views = NULL;
        if (!createArtSwapchain(ctx, GLOW_TEX, GLOW_TEX, "create glow swapchain", &ctx->glowSwapchain,
                                &ctx->glowImages, &views, &ctx->glowImageCount)) {
            ctx->glowFailed = 1;
            return;
        }
        // Only ever copied into
        for (uint32_t i = 0; i < ctx->glowImageCount; i++) vkDestroyImageView(ctx->vk->device, views[i], NULL);
        free(views);
        LOGEV("swapchains alive %d with the glow's", ctx->swapchainsAlive);
    }
    uint32_t index = 0;
    if (!xrArtAcquire(ctx->glowSwapchain, "acquire glow image", &index)) {
        return;
    }
    VkCommandBuffer cmd = ctx->frameCmd;
    VkImage dst = ctx->glowImages[index].image;
    mlxr_barrier(cmd, dst, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    xrVkImageTo(ctx, &ctx->glowTexture, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    // The glow's own image is _UNORM holding the gamma encoded bytes upstream
    // stored; copied as they are into the _SRGB swapchain the compositor decodes
    VkImageCopy region;
    memset(&region, 0, sizeof(region));
    region.srcSubresource = (VkImageSubresourceLayers){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.dstSubresource = region.srcSubresource;
    region.extent = (VkExtent3D){ GLOW_TEX, GLOW_TEX, 1 };
    vkCmdCopyImage(cmd, ctx->glowTexture.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    mlxr_barrier(cmd, dst, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    xrVkImageTo(ctx, &ctx->glowTexture, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    ctx->glowAcquired = 1;
    ctx->glowCopied = 1;
}

// Upstream's curved glow (addGlowLayer's cylinder): the same axis as the
// picture, a little inside its radius, GLOW_SCALE times its angle and height
void xrWorldCurvedGlow(XrCtx* ctx, const MlxrTarget* target, const XrView* views, int eyeW, int eyeH) {
    float aspect = (float)ctx->videoHeight / (float)ctx->videoWidth;
    float fit = cylinderFit(ctx->screenWidth, ctx->screenRadius);
    GlowCylinder shape;
    if (!glowCylinderFor(ctx->screenWidth * fit, ctx->screenWidth * aspect * fit, ctx->screenRadius, &shape)) {
        return;
    }
    MlxrPanelParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.src = ctx->glowTexture.view;
    pp.eyes = 2;
    pp.width = shape.radius * shape.centralAngle;
    pp.height = pp.width / shape.aspectRatio;
    pp.radius = shape.radius;
    pp.segments = 96;
    pp.color[0] = pp.color[1] = pp.color[2] = pp.color[3] = 1.0f;
    // The surface's middle, a radius in front of the shared axis
    Vec3 local = { 0.0f, 0.0f, ctx->screenRadius - shape.radius };
    Vec3 off = quatRotate(ctx->screenPose.orientation, local);
    XrPosef surface = ctx->screenPose;
    surface.position.x += off.x;
    surface.position.y += off.y;
    surface.position.z += off.z;
    for (int e = 0; e < 2; e++) {
        xrCurveMvp(views[e].fov, views[e].pose, surface, pp.mvp[e]);
        pp.eyeRect[e].offset.x = e * eyeW;
        pp.eyeRect[e].extent = (VkExtent2D){ (uint32_t)eyeW, (uint32_t)eyeH };
        pp.uvRect[e][0] = (float)shape.rectX / GLOW_TEX;
        pp.uvRect[e][1] = 0.0f;
        pp.uvRect[e][2] = (float)shape.rectWidth / GLOW_TEX;
        pp.uvRect[e][3] = 1.0f;
    }
    mlxr_pass_panel(ctx->passes, ctx->frameCmd, target, &pp);
}

// ---- the click --------------------------------------------------------------------------------

void (*xrClickPlayer)(void) = NULL;

int xrClickFrame(XrCtx* ctx, const float* in) {
    if (in[IN_CLICK] != 0.0f && ctx->clickSoundOn && xrClickPlayer != NULL) {
        xrClickPlayer();
        return 1;
    }
    return 0;
}
