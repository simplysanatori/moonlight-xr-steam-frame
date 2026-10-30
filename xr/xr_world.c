// Moonlight XR Native: the GPU side the room, the glow and the controller
// models share (see xr_world.h). Nothing here is upstream's: it is what GL did
// for upstream behind its calls (object lifetimes, ordering, the swapchain
// images' release) and the GPU timer, which upstream had as GL timer queries
// (natively timestamps around each section of the frame). GPLv3.
#include "xr_world.h"

#include "xr_native.h"

#include <string.h>

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
    // Native: controller models replaced by another kind, freed once the GPU is past their last frame
    struct {
        MlxrModel* model;
        uint64_t serial;
    } retiredModels[RETIRED_MAX];
    int retiredModelCount;
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

// ---- native (lane eye-swapchains): an image per eye ---------------------------------------

XrEyeTargets xrEyesPerImage(const VkImage image[ROOM_EYES], const VkImageView view[ROOM_EYES], VkFormat format,
                            int eyeW, int eyeH) {
    XrEyeTargets t;
    memset(&t, 0, sizeof(t));
    for (int e = 0; e < ROOM_EYES; e++) {
        t.image[e] = image[e];
        t.view[e] = view[e];
    }
    t.format = format;
    t.eyeW = eyeW;
    t.eyeH = eyeH;
    return t;
}

XrEyeTargets xrEyesSideBySide(VkImage image, VkImageView view, VkFormat format, int eyeW, int eyeH) {
    XrEyeTargets t;
    memset(&t, 0, sizeof(t));
    for (int e = 0; e < ROOM_EYES; e++) {
        t.image[e] = image;
        t.view[e] = view;
        t.x[e] = e * eyeW;
    }
    t.format = format;
    t.eyeW = eyeW;
    t.eyeH = eyeH;
    return t;
}

int xrEyesShared(const XrEyeTargets* t) {
    return t->view[0] == t->view[1];
}

MlxrTarget xrEyeTarget(const XrEyeTargets* t, int eye) {
    MlxrTarget m;
    memset(&m, 0, sizeof(m));
    m.view = t->view[eye];
    m.format = t->format;
    m.rect.offset.x = t->x[eye];
    m.rect.extent = (VkExtent2D){ (uint32_t)t->eyeW, (uint32_t)t->eyeH };
    m.clear = t->clear;
    memcpy(m.clearColor, t->clearColor, sizeof(m.clearColor));
    return m;
}

void xrEyesBarrier(VkCommandBuffer cmd, const XrEyeTargets* t) {
    for (int e = 0; e < ROOM_EYES; e++) {
        if (t->image[e] != VK_NULL_HANDLE && (e == 0 || t->image[e] != t->image[0])) {
            mlxr_barrier(cmd, t->image[e], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        }
    }
}

int xrEyesWorldDepth(XrCtx* ctx, XrEyeTargets* t) {
    int w = xrEyesShared(t) ? t->eyeW * ROOM_EYES : t->eyeW;
    t->depthView = xrWorldDepth(ctx, w, t->eyeH);
    t->depthImage = t->depthView != VK_NULL_HANDLE ? ctx->worldDepth.image : VK_NULL_HANDLE;
    return t->depthView != VK_NULL_HANDLE;
}

void xrEyesPanel(XrCtx* ctx, VkCommandBuffer cmd, const XrEyeTargets* t, const MlxrPanelParams* pp,
                 const VkImageView* src) {
    MlxrPanelParams p = *pp;
    if (xrEyesShared(t) && (src == NULL || src[0] == src[1])) {
        // Both halves of one image in one pass, as before
        MlxrTarget whole = xrEyeTarget(t, 0);
        whole.rect.extent.width = (uint32_t)(t->eyeW * ROOM_EYES);
        p.eyes = ROOM_EYES;
        if (src != NULL) p.src = src[0];
        for (int e = 0; e < ROOM_EYES; e++) {
            p.eyeRect[e].offset.x = t->x[e];
            p.eyeRect[e].offset.y = 0;
            p.eyeRect[e].extent = (VkExtent2D){ (uint32_t)t->eyeW, (uint32_t)t->eyeH };
        }
        mlxr_pass_panel(ctx->passes, cmd, &whole, &p);
        return;
    }
    // An eye at a time, each into its own image (or its own half)
    for (int e = 0; e < ROOM_EYES; e++) {
        MlxrTarget one = xrEyeTarget(t, e);
        p.eyes = 1;
        p.src = src != NULL ? src[e] : pp->src;
        memcpy(p.mvp[0], pp->mvp[e], sizeof(p.mvp[0]));
        memcpy(p.uvRect[0], pp->uvRect[e], sizeof(p.uvRect[0]));
        p.eyeRect[0] = one.rect;
        mlxr_pass_panel(ctx->passes, cmd, &one, &p);
    }
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

// Native: a controller model that another has replaced (the Show controllers row changed kind),
// freed when the GPU has finished the frames that drew it. Its upload, if still waiting on its
// staging, is let go with it
static void retireModel(XrCtx* ctx, MlxrModel* model, uint64_t lastUse) {
    XrWorld* w = world(ctx);
    for (int i = 0; i < w->uploadCount; i++) {
        if (w->uploads[i].model == model) {
            w->uploads[i].model = NULL;
        }
    }
    if (w->retiredModelCount >= RETIRED_MAX) {
        vkDeviceWaitIdle(ctx->vk->device);
        MlxrGpu g = xrWorldGpu(ctx);
        mlxr_model_unload(&g, model);
        return;
    }
    w->retiredModels[w->retiredModelCount].model = model;
    w->retiredModels[w->retiredModelCount].serial = lastUse;
    w->retiredModelCount++;
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
    keep = 0;
    for (int i = 0; i < w->retiredModelCount; i++) {
        if (all || w->retiredModels[i].serial <= ctx->gpuDone) {
            mlxr_model_unload(&g, w->retiredModels[i].model);
            continue;
        }
        w->retiredModels[keep++] = w->retiredModels[i];
    }
    w->retiredModelCount = keep;
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
        memset(ticks, 0, sizeof(ticks));
        // M6 fix: a section not drawn last frame leaves its reset queries
        // unavailable, and the call says VK_NOT_READY for the lot; the ones
        // that were written (w->written, the GPU being past that frame) are
        // still filled in, so they count (before, only frames that ran every
        // section had GPU times)
        VkResult qr = vkGetQueryPoolResults(ctx->vk->device, w->queries, 0, 2 * GPU_SECTIONS, sizeof(ticks), ticks,
                                            sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
        if (qr == VK_SUCCESS || qr == VK_NOT_READY) {
            // M6: what upstream's warp query window held (the depth passes,
            // the colour sample and glow, the warp) goes to the frame budget
            // as the GPU time, the room's as its own (rateBudgetGpu/Room)
            double pictureMs = 0.0;
            int picture = 0;
            for (int s = 0; s < GPU_SECTIONS; s++) {
                if (!(w->written & (1u << s)) || ticks[2 * s + 1] < ticks[2 * s]) continue;
                double ms = (double)(ticks[2 * s + 1] - ticks[2 * s]) * w->tickNs / 1e6;
                if (ms > 50.0) continue; // upstream's plausibility filter
                w->sumMs[s] += ms;
                if (ms > w->maxMs[s]) w->maxMs[s] = ms;
                w->samples[s]++;
                if (s == GPU_WARP) picture = 1;
                if (s == GPU_WARP || s == GPU_GLOW || s == GPU_DOWNSCALE || s == GPU_UPSAMPLE || s == GPU_OFFSET) {
                    pictureMs += ms;
                }
                if (s == GPU_ROOM) {
                    rateBudgetRoom(&ctx->rateBudget, (int64_t)(ms * 1e6));
                }
            }
            if (picture) {
                rateBudgetGpu(&ctx->rateBudget, (int64_t)(pictureMs * 1e6));
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

double xrGpuTimeAvg(XrCtx* ctx, int section, int* samples) {
    XrWorld* w = world(ctx);
    if (samples) *samples = w->samples[section];
    return w->samples[section] ? w->sumMs[section] / w->samples[section] : 0.0;
}

void xrGpuTimesLine(XrCtx* ctx, char* buf, int len) {
    static const char* const NAMES[GPU_SECTIONS] = { "warp", "glow", "room", "models", "curve", "downscale",
                                                         "upsample", "offset" };
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
// Native: of the kind asked for (the first load, and each change of kind after it, on this the
// render thread and into this frame's command buffer, the thread and buffer every other upload
// of the world goes through). The model it replaces is freed once the GPU is past the frames that
// drew it; one that fails to load leaves the one in use drawn.
static void loadControllerModel(XrCtx* ctx, int want) {
    MlxrGpu g = xrWorldGpu(ctx);
    MlxrModel* m = NULL;
    char userDir[600];
    // The user's own models folder: the custom kind's files, and a gun of their own over the bundled one
    const char* user = want == CONTROLLER_MODEL_CUSTOM || want == CONTROLLER_MODEL_GUN
                           ? controllerModelUserDir(userDir, sizeof(userDir))
                           : NULL;
    ctx->modelKindAsked = want;
    if (mlxr_model_record_kind(&g, ctx->assetsDir, user, want, ctx->frameCmd, &m) != 0) {
        if (ctx->model == NULL) {
            ctx->modelLoadFailed = 1;
        }
        LOGW("controller model (%s) is not a mesh the renderer reads (%s/models/controller.room)",
             controllerModelKindName(want), ctx->assetsDir);
        return;
    }
    xrWorldQueueUpload(ctx, VK_NULL_HANDLE, VK_NULL_HANDLE, NULL, m);
    if (ctx->model != NULL) {
        retireModel(ctx, ctx->model, ctx->gpuSerial);
    }
    ctx->model = m;
    ctx->modelReady = 1;
    ctx->modelLoadFailed = 0;
    // The pass's pipeline for this model (the textured one is not the painted one)
    if (ctx->modelPassReady && ctx->passes != NULL) {
        mlxr_passes_warm(ctx->passes, m->textured ? MLXR_PASS_MODEL_TEX : MLXR_PASS_MODEL, MLXR_VIDEO_YUV420P,
                         VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_D32_SFLOAT);
    }
    // Native: which model it is, and from what
    LOGEV("controller model ready: %s asked, %s drawn at the %s pose, %s, from %s: %u vertices, %u triangles, "
          "%.0f KB, %.1f ms, %s", controllerModelKindName(want), controllerModelKindName(m->kind),
          m->space == MODEL_SPACE_AIM ? "aim" : "grip",
          m->textured ? (m->mirrorLeft ? "one textured model mirrored for the left hand" : "per-hand, textured")
                      : m->perHand ? "per-hand, painted" : "painted, mirrored for the left hand",
          m->source, m->vertexCount, m->indexCount / 3, m->gpuBytes / 1024.0, m->loadMs,
          ctx->modelOn ? "shown" : "off for now");
    if (m->kind == CONTROLLER_MODEL_GUN) {
        size_t ul = user != NULL ? strlen(user) : 0;
        LOGEV("controller model: the gun is %s", ul > 0 && strncmp(m->source, user, ul) == 0
                                                     ? "your own (gun_left.model and gun_right.model in the models folder)"
                                                     : "the bundled one");
    }
    if (m->textured) {
        LOGEV("controller model textures %ux%u, %u mip levels, components %u left, %u right", m->texWidth,
              m->texHeight, m->texLevels, m->componentCount[0], m->componentCount[1]);
    }
}

void xrWorldPlanLayers(XrCtx* ctx, int pictureProjected) {
    // MLXR_ONE_PROJECTION=0 puts back a layer for each, for diagnosis only
    static int one = -1;
    static int ownPassthrough = -1;
    if (one < 0) {
        const char* e = getenv("MLXR_ONE_PROJECTION");
        one = !(e != NULL && *e == '0');
        // And passthrough's own transparent layer kept under the picture's
        e = getenv("MLXR_PASSTHROUGH_OWN_LAYER");
        ownPassthrough = e != NULL && *e == '1';
        if (!one || ownPassthrough) {
            LOGW("projection layers: one a frame %s, passthrough's own layer %s (environment)", one ? "on" : "off",
                 ownPassthrough ? "kept" : "folded");
        }
    }
    int room = roomEffective(ctx) > 0 && !ctx->passthrough;
    int was = ctx->roomInPicture | (ctx->modelsInPicture << 1) | (ctx->modelsInRoom << 2)
            | (ctx->passthroughInPicture << 3);
    ctx->roomInPicture = one && pictureProjected && room;
    ctx->modelsInPicture = one && pictureProjected && ctx->modelsShowing;
    ctx->modelsInRoom = one && !pictureProjected && room && ctx->modelsShowing;
    // Native (lane wave3-visual): passthrough's layer, which is also the void's
    // colour when it has one
    float backdrop[4];
    ctx->passthroughInPicture = one && !ownPassthrough && pictureProjected
            && (ctx->passthrough || xrBackdropColor(ctx, 0, backdrop));
    int now = ctx->roomInPicture | (ctx->modelsInPicture << 1) | (ctx->modelsInRoom << 2)
            | (ctx->passthroughInPicture << 3);
    if (now != was) {
        LOGI("one projection layer: %s%s%s%s", ctx->roomInPicture ? "room in the picture's layer; " : "",
             ctx->modelsInPicture ? "models in the picture's layer; " : "",
             ctx->modelsInRoom ? "models in the room's layer; " : "",
             ctx->passthroughInPicture ? "the picture's layer is passthrough's; " : now ? "" : "each its own");
    }
}

void xrWorldFrameBegin(XrCtx* ctx) {
    collect(ctx, 0);
    timesCollect(ctx);
    if (!ctx->ambiReady && !ctx->ambiFailed) {
        initAmbilight(ctx);
    }
    // The first frame the switch is on, and any frame its kind is not the one the model was
    // loaded for
    int want = controllerModelKind(ctx->modelOn, ctx->modelKind);
    if (want != CONTROLLER_MODEL_OFF && want != ctx->modelKindAsked) {
        loadControllerModel(ctx, want);
    }
    if (roomEffective(ctx) > 0) {
        prepareRoom(ctx);
    }
}

void xrWorldReleaseImages(XrCtx* ctx) {
    XrSwapchainImageReleaseInfo rl = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    // Native (lane eye-swapchains): an image of each eye's swapchain
    for (int e = 0; e < ROOM_EYES; e++) {
        if (ctx->roomAcquired & (1 << e)) {
            checkXr(xrReleaseSwapchainImage(ctx->roomSwapchain[e], &rl), "release room image");
        }
        if (ctx->modelAcquired & (1 << e)) {
            checkXr(xrReleaseSwapchainImage(ctx->modelSwapchain[e], &rl), "release controller model image");
        }
    }
    ctx->roomAcquired = 0;
    ctx->modelAcquired = 0;
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
    ctx->modelKindAsked = 0;
    for (int e = 0; e < ROOM_EYES; e++) {
        destroyArtSwapchain(ctx, &ctx->roomSwapchain[e], &ctx->roomImages[e], &ctx->roomImageViews[e],
                            &ctx->roomImageCount[e]);
        destroyArtSwapchain(ctx, &ctx->modelSwapchain[e], &ctx->modelImages[e], &ctx->modelImageViews[e],
                            &ctx->modelImageCount[e]);
    }
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
    // (native, PLAN 4.3: not on a host-rendered pair)
    if (sampled && ambiBarDetectOn(ctx)) {
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
void xrWorldCurvedGlow(XrCtx* ctx, const XrEyeTargets* target, const XrView* views) {
    float aspect = (float)ctx->videoHeight / (float)ctx->videoWidth;
    float fit = cylinderFit(ctx->screenWidth, ctx->screenRadius);
    GlowCylinder shape;
    if (!glowCylinderFor(ctx->screenWidth * fit, ctx->screenWidth * aspect * fit, ctx->screenRadius, &shape)) {
        return;
    }
    MlxrPanelParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.src = ctx->glowTexture.view;
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
        pp.uvRect[e][0] = (float)shape.rectX / GLOW_TEX;
        pp.uvRect[e][1] = 0.0f;
        pp.uvRect[e][2] = (float)shape.rectWidth / GLOW_TEX;
        pp.uvRect[e][3] = 1.0f;
    }
    // Native (lane eye-swapchains): each eye into its own image
    xrEyesPanel(ctx, ctx->frameCmd, target, &pp, NULL);
}

void xrWorldFlatGlowInto(XrCtx* ctx, const XrEyeTargets* target, const XrView* views) {
    float aspect = (float)ctx->videoHeight / (float)ctx->videoWidth;
    MlxrPanelParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.src = ctx->glowTexture.view;
    // The glow quad's size and place (addGlowLayer), flat
    pp.width = ctx->screenWidth * GLOW_SCALE;
    pp.height = ctx->screenWidth * aspect * GLOW_SCALE;
    pp.radius = 0.0f;
    pp.segments = 1;
    pp.color[0] = pp.color[1] = pp.color[2] = pp.color[3] = 1.0f;
    for (int e = 0; e < 2; e++) {
        xrCurveMvp(views[e].fov, views[e].pose, ctx->screenPose, pp.mvp[e]);
        pp.uvRect[e][0] = 0.0f;
        pp.uvRect[e][1] = 0.0f;
        pp.uvRect[e][2] = 1.0f;
        pp.uvRect[e][3] = 1.0f;
    }
    xrEyesPanel(ctx, ctx->frameCmd, target, &pp, NULL);
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
