// Moonlight XR Native: realtime 2D to 3D (M6), see xr_depth.h. Upstream's
// xr_depth.c and the 3D half of xr_gl.c's renderVideoFrame (Gilleece, GPLv3),
// rewritten for Vulkan: GL's second and third contexts, pixel buffers and
// fences become a host visible buffer per capture keyed by the frame's
// timeline value, a stage thread that waits on it, and the depth engine's own
// thread (depth/depth_engine.c, lane D) in place of Java's. What stays
// upstream's: when a capture is taken and skipped, DEPTH_PAIRS captures in
// flight, three depth textures in a fixed rotation, the upsample on every
// video frame, the offset search only with occlusion and a separation, the
// warp's per eye disparity, convergence, edge fade, held source and cubic
// read, the flat draw while the 3D waits for a fresh map, and the test
// patterns. Every change is marked "native".
#define _GNU_SOURCE
#include "xr_depth.h"

#include <sched.h>

#include "settings.h"
#include "ui_strings.h"
#include "xr_native.h"
#include "xr_vk.h"
#include "xr_world.h"

// ---- the engine -----------------------------------------------------------------

// None until the host hands the real one in (xr_depth_engine.c, built only
// with the depth library), so the tests that link the renderer core do not
// pull LiteRT in
static const XrDepthEngineOps* g_ops;

const XrDepthEngineOps* xrDepthEngineOps(void) {
    return g_ops;
}

void xrDepthSetEngineOps(const XrDepthEngineOps* ops) {
    g_ops = ops;
}

// ---- state -------------------------------------------------------------------------

#define PAIR_FREE 0
#define PAIR_RECORDED 1   // the frame loop recorded a copy into it, not yet submitted
#define PAIR_QUEUED 2     // submitted, for the stage thread
#define PAIR_STAGING 3    // the stage thread has it

#define DEPTH_IDLE -1     // engine not started (or stopped)

typedef struct HostBuf {
    VkBuffer buf;
    VkDeviceMemory mem;
    uint8_t* map;
    VkDeviceSize size;
    int coherent;
} HostBuf;

// One capture's trip: the readback buffer and what it was taken from
typedef struct DepthPair {
    HostBuf hb;
    int state;
    uint64_t value;       // the timeline value the copy has landed at
    uint64_t frameId;
    int64_t captureNs;
} DepthPair;

// One depth texture of the rotation and the staging its maps go up through
typedef struct DepthTex {
    XrVkImage img;
    HostBuf staging;
    uint64_t lastUse;     // the serial of the last frame that wrote or sampled it
    uint64_t frameId, seq;
    int64_t captureNs, submitNs;
    int valid;
} DepthTex;

typedef struct XrDepth {
    const XrDepthEngineOps* ops;
    MlxrDepthEngine* engine;
    int state;                // DEPTH_IDLE or MlxrDepthState
    int64_t startNs;
    char modelName[64];
    int w, h;                 // the map's size
    int gpuReady;             // the images and buffers below are made
    int gpuFailed;
    XrVkImage downscale;
    DepthPair pairs[DEPTH_PAIRS];
    DepthTex tex[DEPTH_TEX_COUNT];
    int readIndex, writeIndex;
    int texValid;             // a map (or a test pattern) is in tex[readIndex]
    uint64_t haveSeq;
    XrVkImage upsample, offsets;
    // the stage thread
    pthread_t stage;
    int stageRunning;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int stop;
    VkDevice device;
    VkSemaphore timeline;
    // the switch, as last seen here
    int wasLive;
    int forceCapture;
    int64_t backNs;           // when the 3D last came back on
    int rateSet;
    float convSet;
    uint64_t cpuMask;
    // counters (frame loop)
    uint64_t captures, skipsNoPair, uploads, uploadBlocked, pairReuseBad, lastFrameId, frameIdBackwards;
    // counters (stage thread, under lock)
    uint64_t submitted, submitFailed, stageRuns;
    double stageWaitMs, submitMs;
    // the stats window
    double ageMsSum, ageFramesSum;
    long ageSamples;
    double ageWorstMs;
    uint64_t winReplaced, winSkips;
    float lastAgeMs;          // smoothed, for the overlay (upstream lastDepthAgeMs)
    MlxrDepthStats lastStats;
    int saidFirstMap;
} XrDepth;

static int64_t nowMono(void) {
    return nowNs();
}

// ---- small Vulkan helpers ------------------------------------------------------------

static int hostBufMake(XrCtx* ctx, HostBuf* b, VkDeviceSize size, VkBufferUsageFlags usage, int readback) {
    const XrVk* vk = ctx->vk;
    memset(b, 0, sizeof(*b));
    VkBufferCreateInfo bi = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bi.size = size;
    bi.usage = usage;
    if (vkCreateBuffer(vk->device, &bi, NULL, &b->buf) != VK_SUCCESS) {
        return 0;
    }
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(vk->device, b->buf, &req);
    // A readback is read by the CPU, so cached memory if there is any
    // (invalidated when it is not coherent); a staging buffer is only written
    const VkMemoryPropertyFlags HV = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    const VkMemoryPropertyFlags HC = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    const VkMemoryPropertyFlags CA = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    VkMemoryPropertyFlags tries[3] = { HV | HC | CA, HV | CA, HV | HC };
    if (!readback) {
        tries[0] = HV | HC;
        tries[1] = HV | HC;
        tries[2] = HV | CA;
    }
    uint32_t type = UINT32_MAX;
    for (int i = 0; i < 3 && type == UINT32_MAX; i++) {
        type = xr_vk_memory_type(vk, req.memoryTypeBits, tries[i]);
    }
    if (type == UINT32_MAX) {
        vkDestroyBuffer(vk->device, b->buf, NULL);
        b->buf = VK_NULL_HANDLE;
        return 0;
    }
    VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = type;
    if (vkAllocateMemory(vk->device, &ai, NULL, &b->mem) != VK_SUCCESS) {
        vkDestroyBuffer(vk->device, b->buf, NULL);
        b->buf = VK_NULL_HANDLE;
        return 0;
    }
    vkBindBufferMemory(vk->device, b->buf, b->mem, 0);
    void* map = NULL;
    if (vkMapMemory(vk->device, b->mem, 0, VK_WHOLE_SIZE, 0, &map) != VK_SUCCESS) {
        return 0;
    }
    b->map = map;
    b->size = size;
    b->coherent = (vk->mem.memoryTypes[type].propertyFlags & HC) != 0;
    return 1;
}

static void hostBufFree(XrCtx* ctx, HostBuf* b) {
    if (b->mem) {
        if (b->map) vkUnmapMemory(ctx->vk->device, b->mem);
        vkFreeMemory(ctx->vk->device, b->mem, NULL);
    }
    if (b->buf) vkDestroyBuffer(ctx->vk->device, b->buf, NULL);
    memset(b, 0, sizeof(*b));
}

// ---- configuration -------------------------------------------------------------------

// config.ini's depth_source to upstream's DEPTH_MODE_ and the model
// (settings.c DEPTH_NAMES: zipdepth, midas, off, flat, ramp, blob, eyetest,
// shifttest, zipdepth-int8)
static int modeForSource(int source, int* model) {
    *model = -1;
    switch (source) {
    case 0: *model = MLXR_DEPTH_ZIPDEPTH_512x288_FP16; return DEPTH_MODE_MODEL;
    case 1: *model = MLXR_DEPTH_MIDAS_256_FP16; return DEPTH_MODE_MODEL;
    case 8: *model = MLXR_DEPTH_ZIPDEPTH_256_INT8DR; return DEPTH_MODE_MODEL;
    case 3: return DEPTH_MODE_FLAT;
    case 4: return DEPTH_MODE_RAMP;
    case 5: return DEPTH_MODE_BLOB;
    case 6: return DEPTH_MODE_EYETEST;
    case 7: return DEPTH_MODE_SHIFTTEST;
    default: return DEPTH_MODE_OFF;
    }
}

const char* xrDepthModelPath(XrCtx* ctx) {
    static char path[640];
    path[0] = '\0';
    const XrDepthEngineOps* ops = g_ops;
    if (ops == NULL || ctx->depthModel < 0 || ctx->assetsDir[0] == '\0') {
        return path;
    }
    const MlxrDepthModelSpec* spec = ops->spec((MlxrDepthModelId)ctx->depthModel);
    if (spec == NULL) {
        return path;
    }
    snprintf(path, sizeof(path), "%s/depth/%s", ctx->assetsDir, spec->file);
    return path;
}

void xrDepthConfigure(XrCtx* ctx, const Settings* s) {
    int model;
    int mode = modeForSource(s->depth_source, &model);
    ctx->depthDebug = s->depth_debug;
    ctx->depthModel = model;
    ctx->depthReason[0] = '\0';
    // The map's size: the model's input (upstream's session size), and
    // ZipDepth's for the test patterns
    ctx->depthTexW = 512;
    ctx->depthTexH = 288;
    if (mode == DEPTH_MODE_OFF) {
        snprintf(ctx->depthReason, sizeof(ctx->depthReason), "%s", ui_str(UIS_THREE_D_OFF_IN_SETTINGS));
    }
    if (mode == DEPTH_MODE_MODEL) {
        // Native: only with the depth library built in and the model's file
        // where the assets are; otherwise flat, saying why
        const XrDepthEngineOps* ops = g_ops;
        const MlxrDepthModelSpec* spec = ops != NULL ? ops->spec((MlxrDepthModelId)model) : NULL;
        const char* path = xrDepthModelPath(ctx);
        if (spec == NULL || path[0] == '\0' || access(path, R_OK) != 0) {
            LOGW("3d: depth model %s unavailable (%s), the picture stays flat",
                 spec != NULL ? spec->name : "?", ops == NULL ? "no depth library in this build"
                                                                : path[0] ? path : "no assets directory");
            snprintf(ctx->depthReason, sizeof(ctx->depthReason), "%s", ui_str(UIS_NATIVE_3D_NO_MODEL));
            mode = DEPTH_MODE_OFF;
        }
        else {
            ctx->depthTexW = spec->width;
            ctx->depthTexH = spec->height;
        }
    }
    ctx->stereoMode = mode;
    // Native: the switch as it was last left (upstream: on in every session
    // with stereo, for that session only)
    ctx->stereoLive = mode != DEPTH_MODE_OFF && s->stereo;
    ctx->stereoWaiting = 0;
    ctx->drawnEyes = 1;
    ctx->depthFailed = 0;
    atomic_store_explicit(&ctx->depthGaveUp, 0, memory_order_relaxed);
    LOGEV("3d: %s, depth map %dx%d, 3D %s at start%s", mode == DEPTH_MODE_MODEL ? "depth model"
          : mode == DEPTH_MODE_OFF ? "off" : "test pattern", ctx->depthTexW, ctx->depthTexH,
          ctx->stereoLive ? "on" : "off", mode == DEPTH_MODE_MODEL ? "" : "");
}

int xrDepthChainWidth(XrCtx* ctx, int pictureWidth) {
    return ctx->stereoMode != DEPTH_MODE_OFF ? pictureWidth * 2 : pictureWidth;
}

// ---- the stage thread (upstream runDepthStage's finish half) -------------------------

static void* stageMain(void* arg) {
    XrDepth* d = arg;
    const XrDepthEngineOps* ops = d->ops;
    if (d->cpuMask != 0) {
        cpu_set_t set;
        CPU_ZERO(&set);
        for (int c = 0; c < 64 && c < CPU_SETSIZE; c++) {
            if (d->cpuMask & (1ull << c)) CPU_SET(c, &set);
        }
        pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
    }
    pthread_mutex_lock(&d->lock);
    for (;;) {
        // The oldest capture first, so maps go up in the order their frames came
        int pick = -1;
        for (int i = 0; i < DEPTH_PAIRS; i++) {
            if (d->pairs[i].state == PAIR_QUEUED && (pick < 0 || d->pairs[i].value < d->pairs[pick].value)) {
                pick = i;
            }
        }
        if (pick < 0) {
            if (d->stop) break;
            pthread_cond_wait(&d->cond, &d->lock);
            continue;
        }
        DepthPair* p = &d->pairs[pick];
        p->state = PAIR_STAGING;
        uint64_t value = p->value;
        pthread_mutex_unlock(&d->lock);

        // The frame loop only queued the copy; this is where it is waited on,
        // on a thread with no frame to miss
        int64_t t0 = nowMono();
        int landed = 0, warned = 0;
        for (;;) {
            VkSemaphoreWaitInfo wi = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
            wi.semaphoreCount = 1;
            wi.pSemaphores = &d->timeline;
            wi.pValues = &value;
            VkResult r = vkWaitSemaphores(d->device, &wi, 100000000ull);
            if (r == VK_SUCCESS) {
                landed = 1;
                break;
            }
            if (r != VK_TIMEOUT) {
                LOGW("depth capture: waiting for the readback failed (%d)", (int)r);
                break;
            }
            pthread_mutex_lock(&d->lock);
            int stop = d->stop;
            pthread_mutex_unlock(&d->lock);
            if (stop) break;
            if (!warned && nowMono() - t0 > (int64_t)CAPTURE_FENCE_TIMEOUT_NS) {
                LOGW("depth capture: readback not landed after %.0f ms, still waiting",
                     CAPTURE_FENCE_TIMEOUT_NS / 1e6);
                warned = 1;
            }
        }
        int64_t t1 = nowMono();
        int rc = -1;
        if (landed) {
            if (!p->hb.coherent) {
                VkMappedMemoryRange mr = { VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE };
                mr.memory = p->hb.mem;
                mr.size = VK_WHOLE_SIZE;
                vkInvalidateMappedMemoryRanges(d->device, 1, &mr);
            }
            // Converted to the model's input and through the scene cut
            // detector in there, as nativeFinishDepthCapture did; copied
            // before it returns, so the buffer is free again after it
            rc = ops->submit(d->engine, p->hb.map, (size_t)d->w * 4, d->w, d->h, 0, p->frameId, p->captureNs);
        }
        int64_t t2 = nowMono();
        pthread_mutex_lock(&d->lock);
        p->state = PAIR_FREE;
        if (landed) {
            d->stageRuns++;
            d->stageWaitMs += (t1 - t0) / 1e6;
            d->submitMs += (t2 - t1) / 1e6;
            if (rc >= 0) d->submitted++;
            else d->submitFailed++;
        }
        if (!landed && d->stop) break;
    }
    pthread_mutex_unlock(&d->lock);
    return NULL;
}

// ---- GPU resources ------------------------------------------------------------------

static void freeUpsample(XrCtx* ctx, XrDepth* d) {
    xrVkImageFree(ctx, &d->upsample);
    xrVkImageFree(ctx, &d->offsets);
    memset(&d->upsample, 0, sizeof(d->upsample));
    memset(&d->offsets, 0, sizeof(d->offsets));
}

static void freeGpu(XrCtx* ctx, XrDepth* d) {
    if (d->downscale.image) xrVkImageFree(ctx, &d->downscale);
    memset(&d->downscale, 0, sizeof(d->downscale));
    for (int i = 0; i < DEPTH_PAIRS; i++) {
        hostBufFree(ctx, &d->pairs[i].hb);
        d->pairs[i].state = PAIR_FREE;
    }
    for (int i = 0; i < DEPTH_TEX_COUNT; i++) {
        if (d->tex[i].img.image) xrVkImageFree(ctx, &d->tex[i].img);
        hostBufFree(ctx, &d->tex[i].staging);
        memset(&d->tex[i], 0, sizeof(d->tex[i]));
    }
    if (d->upsample.image || d->offsets.image) freeUpsample(ctx, d);
    d->gpuReady = 0;
    d->texValid = 0;
}

// The depth textures and their staging, at the map's size (both the model's
// maps and the test patterns go up through them)
static int makeTextures(XrCtx* ctx, XrDepth* d) {
    size_t bytes = (size_t)d->w * d->h * 4;
    for (int i = 0; i < DEPTH_TEX_COUNT; i++) {
        if (!xrVkImageMake(ctx, &d->tex[i].img, d->w, d->h, MLXR_PASS_FORMAT,
                           VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
                                   | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                           VK_IMAGE_ASPECT_COLOR_BIT)
                || !hostBufMake(ctx, &d->tex[i].staging, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 0)) {
            return 0;
        }
    }
    // Slot 0 is the first read, so the first real upload lands somewhere else
    d->readIndex = 0;
    d->writeIndex = 1;
    return 1;
}

// What the model path needs once the engine is ready: the downscale target,
// the readbacks, the textures (upstream initDepthModel)
static int makeModelGpu(XrCtx* ctx, XrDepth* d) {
    size_t bytes = (size_t)d->w * d->h * 4;
    if (!xrVkImageMake(ctx, &d->downscale, d->w, d->h, MLXR_PASS_FORMAT,
                       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                               | VK_IMAGE_USAGE_SAMPLED_BIT,
                       VK_IMAGE_ASPECT_COLOR_BIT)) {
        return 0;
    }
    for (int i = 0; i < DEPTH_PAIRS; i++) {
        if (!hostBufMake(ctx, &d->pairs[i].hb, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, 1)) {
            return 0;
        }
        d->pairs[i].state = PAIR_FREE;
    }
    if (!makeTextures(ctx, d)) {
        return 0;
    }
    LOGI("depth model staging ready at %dx%d (%d readbacks %s, %d textures)", d->w, d->h, DEPTH_PAIRS,
         d->pairs[0].hb.coherent ? "coherent" : "invalidated", DEPTH_TEX_COUNT);
    return 1;
}

// Upstream's fillSyntheticDepth: the test patterns, into every texture.
// Depth convention: 0 far, 1 near, 0.5 on the screen plane. No guide colour,
// so it stays neutral.
static int fillSynthetic(XrCtx* ctx, XrDepth* d) {
    const int w = d->w, h = d->h;
    VkCommandBuffer cmd = ctx->frameCmd;
    for (int i = 0; i < DEPTH_TEX_COUNT; i++) {
        uint8_t* buf = d->tex[i].staging.map;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                float fx = x / (float)(w - 1);
                float fy = y / (float)(h - 1);
                float v;
                switch (ctx->stereoMode) {
                case DEPTH_MODE_RAMP:
                    v = fx;
                    break;
                case DEPTH_MODE_BLOB: {
                    float dx = fx - 0.5f;
                    float dy = fy - 0.5f;
                    float sigma = 0.15f;
                    v = 0.35f + 0.5f * expf(-(dx * dx + dy * dy) / (2.0f * sigma * sigma));
                    break;
                }
                case DEPTH_MODE_SHIFTTEST:
                    // Constant near depth so the whole bar shifts uniformly
                    v = 0.85f;
                    break;
                case DEPTH_MODE_FLAT:
                default:
                    v = 0.5f;
                    break;
                }
                if (v < 0.0f) v = 0.0f;
                if (v > 1.0f) v = 1.0f;
                uint8_t* px = buf + ((size_t)y * w + x) * 4;
                px[0] = px[1] = px[2] = 128;
                px[3] = (uint8_t)(v * 255.0f + 0.5f);
            }
        }
        XrVkImage* img = &d->tex[i].img;
        xrVkImageTo(ctx, img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkBufferImageCopy region = { 0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 },
                                     { (uint32_t)w, (uint32_t)h, 1 } };
        vkCmdCopyBufferToImage(cmd, d->tex[i].staging.buf, img->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                               &region);
        xrVkImageTo(ctx, img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        d->tex[i].lastUse = ctx->gpuSerial;
        d->tex[i].valid = 1;
    }
    d->texValid = 1;
    d->readIndex = 0;
    return 1;
}

// Quarter resolution is enough: at 1920x1080 the measured edge width was the
// same 5 px, so the extra four times the pixels bought nothing (upstream
// initUpsample)
static int ensureUpsample(XrCtx* ctx, XrDepth* d, int videoW, int videoH) {
    int uw = videoW / 4, uh = videoH / 4;
    if (uw < 1) uw = 1;
    if (uh < 1) uh = 1;
    if (d->upsample.image && d->upsample.width == uw && d->upsample.height == uh) {
        return 1;
    }
    if (d->upsample.image || d->offsets.image) {
        // The video's size changed: the frame before may still sample them
        vkDeviceWaitIdle(ctx->vk->device);
        freeUpsample(ctx, d);
    }
    VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
            | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (!xrVkImageMake(ctx, &d->upsample, uw, uh, MLXR_PASS_FORMAT, usage, VK_IMAGE_ASPECT_COLOR_BIT)
            || !xrVkImageMake(ctx, &d->offsets, uw, uh, MLXR_PASS_FORMAT, usage, VK_IMAGE_ASPECT_COLOR_BIT)) {
        LOGE("depth upsample images %dx%d could not be made", uw, uh);
        freeUpsample(ctx, d);
        return 0;
    }
    ctx->upsampleWidth = uw;
    ctx->upsampleHeight = uh;
    LOGI("depth upsample and offset search ready at %dx%d", uw, uh);
    return 1;
}

static XrDepth* depthState(XrCtx* ctx) {
    if (ctx->depth == NULL) {
        XrDepth* d = calloc(1, sizeof(XrDepth));
        if (d == NULL) return NULL;
        d->state = DEPTH_IDLE;
        d->rateSet = -1;
        d->convSet = -1.0f;
        pthread_mutex_init(&d->lock, NULL);
        pthread_cond_init(&d->cond, NULL);
        ctx->depth = d;
    }
    return ctx->depth;
}

// "3-6", "7", "3,5,7" or a hex mask ("0x78")
static uint64_t parseCpus(const char* s) {
    if (s == NULL || *s == '\0') return 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) return strtoull(s, NULL, 16);
    uint64_t mask = 0;
    const char* p = s;
    while (*p) {
        char* end;
        long a = strtol(p, &end, 10);
        if (end == p) break;
        long b = a;
        p = end;
        if (*p == '-') {
            b = strtol(p + 1, &end, 10);
            p = end;
        }
        for (long c = a; c <= b && c < 64; c++) {
            if (c >= 0) mask |= 1ull << c;
        }
        if (*p == ',') p++;
    }
    return mask;
}

// ---- lifecycle -----------------------------------------------------------------------

static void stopStage(XrDepth* d) {
    if (!d->stageRunning) return;
    pthread_mutex_lock(&d->lock);
    d->stop = 1;
    pthread_cond_broadcast(&d->cond);
    pthread_mutex_unlock(&d->lock);
    pthread_join(d->stage, NULL);
    d->stageRunning = 0;
    d->stop = 0;
}

static void engineStop(XrCtx* ctx, XrDepth* d) {
    stopStage(d);
    if (d->engine != NULL) {
        d->ops->stop(d->engine);
        d->engine = NULL;
        LOGEV("depth model stopped");
    }
    (void)ctx;
}

// The model will make no map this session: flat, with a toast saying so, and
// the 3D switch put away (native: upstream left the splash waiting on it and
// drew the flat synthetic map)
static void depthGaveUp(XrCtx* ctx, XrDepth* d, const char* why) {
    LOGE("3d: the depth model gave up (%s), the picture stays flat", why);
    engineStop(ctx, d);
    d->state = MLXR_DEPTH_FAILED;
    atomic_store_explicit(&ctx->depthGaveUp, 1, memory_order_relaxed);
    ctx->depthFailed = 1;
    ctx->stereoLive = 0;
    ctx->stereoWaiting = 0;
    ctx->stereoMode = DEPTH_MODE_OFF;
    ctx->stereoButtonReady = 0;
    ctx->warpRedraw = 1;
    snprintf(ctx->depthReason, sizeof(ctx->depthReason), "%s", ui_str(UIS_NATIVE_3D_FAILED));
    xrPanelsNotice(ctx, ui_str(UIS_NATIVE_3D_FAILED));
}

static void engineStart(XrCtx* ctx, XrDepth* d) {
    const XrDepthEngineOps* ops = g_ops;
    const char* path = xrDepthModelPath(ctx);
    const MlxrDepthModelSpec* spec = ops != NULL ? ops->spec((MlxrDepthModelId)ctx->depthModel) : NULL;
    if (ops == NULL || spec == NULL) {
        depthGaveUp(ctx, d, "no depth engine");
        return;
    }
    d->ops = ops;
    MlxrDepthConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.modelPath = path;
    // Upstream's CPU route: two threads, and fp16 arithmetic on these cores
    // (depth/REPORT.md: ZipDepth 512x288 27 maps/s at 2 threads in fp16)
    cfg.threads = 2;
    const char* threads = getenv("MLXR_DEPTH_THREADS");
    if (threads != NULL && atoi(threads) > 0) cfg.threads = atoi(threads);
    cfg.flags = MLXR_DEPTH_FP16;
    const char* fp32 = getenv("MLXR_DEPTH_FP32");
    if (fp32 != NULL && atoi(fp32) != 0) cfg.flags = 0;
    cfg.perSecond = ctx->depthGov.target;
    // Native: which CPUs the model's threads run on (MLXR_DEPTH_CPUS, e.g.
    // "3-6" or "0x78"); unset leaves the scheduler to it (M6_REPORT.md)
    d->cpuMask = parseCpus(getenv("MLXR_DEPTH_CPUS"));
    cfg.cpuMask = d->cpuMask;
    cfg.warmupRuns = 0;
    cfg.params.tauMs = DEPTH_TAU_DEFAULT_MS;
    cfg.params.rangeTauMs = DEPTH_RANGE_TAU_DEFAULT_MS;
    cfg.params.cutLevel = 1;
    cfg.params.global = 1.0f;
    cfg.params.local = 1.0f;
    cfg.params.convergence = ctx->convergence;
    snprintf(d->modelName, sizeof(d->modelName), "%s", spec->name);
    d->startNs = nowMono();
    d->engine = ops->start(&cfg);
    if (d->engine == NULL) {
        depthGaveUp(ctx, d, "the depth thread did not start");
        return;
    }
    d->state = MLXR_DEPTH_LOADING;
    d->rateSet = cfg.perSecond;
    d->convSet = ctx->convergence;
    LOGEV("depth model %s starting: %s, %d threads, %s, %d maps/s, cpus %s", spec->name, path, cfg.threads,
          cfg.flags & MLXR_DEPTH_FP16 ? "fp16" : "fp32", cfg.perSecond,
          d->cpuMask ? getenv("MLXR_DEPTH_CPUS") : "any");
}

// LOADING to READY or FAILED, polled each frame
static void enginePoll(XrCtx* ctx, XrDepth* d) {
    MlxrDepthState st = d->ops->state(d->engine);
    if (st == MLXR_DEPTH_LOADING) {
        return;
    }
    if (st == MLXR_DEPTH_FAILED) {
        char err[160] = "";
        d->ops->waitReady(d->engine, 0, err, sizeof(err));
        depthGaveUp(ctx, d, err[0] ? err : "the model did not load");
        return;
    }
    int w = 0, h = 0;
    d->ops->size(d->engine, &w, &h);
    if (w <= 0 || h <= 0 || !depthSizeOk(w, h)) {
        depthGaveUp(ctx, d, "the model's size is out of range");
        return;
    }
    if (d->gpuReady && (w != d->w || h != d->h)) {
        vkDeviceWaitIdle(ctx->vk->device);
        freeGpu(ctx, d);
    }
    d->w = w;
    d->h = h;
    ctx->depthTexW = w;
    ctx->depthTexH = h;
    if (!d->gpuReady) {
        if (!makeModelGpu(ctx, d)) {
            freeGpu(ctx, d);
            depthGaveUp(ctx, d, "the depth staging could not be made");
            return;
        }
        d->gpuReady = 1;
    }
    d->device = ctx->vk->device;
    d->timeline = ctx->gpuTimeline;
    if (d->timeline == VK_NULL_HANDLE) {
        depthGaveUp(ctx, d, "no frame timeline to wait on");
        return;
    }
    d->stop = 0;
    if (pthread_create(&d->stage, NULL, stageMain, d) != 0) {
        depthGaveUp(ctx, d, "the stage thread did not start");
        return;
    }
    d->stageRunning = 1;
    d->state = MLXR_DEPTH_READY;
    MlxrDepthStats s;
    d->ops->stats(d->engine, &s);
    LOGEV("depth model %s ready after %.0f ms: %dx%d, %d threads, load %.0f ms, warm up %.0f ms",
          d->modelName, (nowMono() - d->startNs) / 1e6, w, h, s.threads, s.loadMs, s.warmupMs);
    // The 3D was on while it loaded: the frame in hand is the first capture
    d->forceCapture = ctx->stereoLive;
}

// The newest map into the next texture of the rotation (upstream's stage
// thread upload, natively a copy in the frame's command buffer)
static void uploadMap(XrCtx* ctx, XrDepth* d) {
    const MlxrDepthResult* r = d->ops->acquire(d->engine, d->haveSeq);
    if (r == NULL) {
        return;
    }
    int slot = d->writeIndex;
    DepthTex* t = &d->tex[slot];
    if (t->lastUse > ctx->gpuDone) {
        // A frame still in flight may read the staging or the texture: left
        // for a later frame (with one frame in flight this never happens)
        d->uploadBlocked++;
        d->ops->release(d->engine, r);
        return;
    }
    if (r->width != d->w || r->height != d->h) {
        d->ops->release(d->engine, r);
        return;
    }
    memcpy(t->staging.map, r->rgba, (size_t)d->w * d->h * 4);
    t->frameId = r->frameId;
    t->seq = r->seq;
    t->captureNs = r->captureNs;
    t->submitNs = r->submitNs;
    d->haveSeq = r->seq;
    d->ops->release(d->engine, r);
    VkCommandBuffer cmd = ctx->frameCmd;
    xrVkImageTo(ctx, &t->img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy region = { 0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 },
                                 { (uint32_t)d->w, (uint32_t)d->h, 1 } };
    vkCmdCopyBufferToImage(cmd, t->staging.buf, t->img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    xrVkImageTo(ctx, &t->img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    t->lastUse = ctx->gpuSerial;
    t->valid = 1;
    if (d->uploads > 0 && t->frameId < d->lastFrameId) {
        d->frameIdBackwards++;
    }
    d->lastFrameId = t->frameId;
    d->uploads++;
    // Always the next slot in the rotation, never a function of where the
    // frame loop reads, which keeps it off a slot a draw could be on
    d->readIndex = slot;
    d->writeIndex = (slot + 1) % DEPTH_TEX_COUNT;
    atomic_fetch_add_explicit(&ctx->depthStagedIndex, 1, memory_order_release);
    if (!d->texValid) {
        // The first map: a picture standing still takes its depth now
        ctx->warpRedraw = 1;
    }
    d->texValid = 1;
    if (!d->saidFirstMap) {
        d->saidFirstMap = 1;
        LOGEV("depth: first map up %.0f ms after the model started (frame %llu)", (nowMono() - d->startNs) / 1e6,
              (unsigned long long)t->frameId);
    }
}

void xrDepthFrame(XrCtx* ctx) {
    // What is in force this frame (upstream nativeEndFrame): the panel's
    // separation over the preference, and the debug knob over both
    float separation = ctx->panelSeparation >= 0.0f ? ctx->panelSeparation : ctx->separationPref;
    if (ctx->separationOverride >= 0.0f) {
        separation = ctx->separationOverride;
    }
    ctx->separationCurrent = separation;
    if (ctx->stereoMode == DEPTH_MODE_OFF || ctx->vk == NULL) {
        return;
    }
    XrDepth* d = depthState(ctx);
    if (d == NULL) return;
    int64_t now = nowMono();

    if (ctx->stereoMode != DEPTH_MODE_MODEL) {
        // A test pattern: its textures, filled once
        if (!d->gpuReady && !d->gpuFailed && ctx->frameCmd != VK_NULL_HANDLE) {
            d->w = ctx->depthTexW;
            d->h = ctx->depthTexH;
            if (!makeTextures(ctx, d)) {
                LOGE("3d: test pattern textures could not be made");
                d->gpuFailed = 1;
                freeGpu(ctx, d);
                return;
            }
            d->gpuReady = 1;
            fillSynthetic(ctx, d);
            ctx->warpRedraw = 1;
            LOGEV("3d: test pattern %d up at %dx%d", ctx->stereoMode, d->w, d->h);
        }
        if (ctx->stereoWaiting) {
            ctx->stereoWaiting = 0;
        }
        return;
    }

    // The model starts the first time the 3D is on (native: not before, so a
    // session that never turns it on never pays for it)
    if (d->state == DEPTH_IDLE && ctx->stereoLive && !ctx->depthFailed) {
        engineStart(ctx, d);
    }
    if (d->state == MLXR_DEPTH_LOADING) {
        enginePoll(ctx, d);
    }
    if (d->state != MLXR_DEPTH_READY) {
        return;
    }

    // Back on: the averages and the cut detector start again, and the frame
    // in hand is captured at once whatever the rate says
    if (ctx->stereoLive && !d->wasLive) {
        d->ops->reset(d->engine);
        d->forceCapture = 1;
        d->backNs = now;
    }
    d->wasLive = ctx->stereoLive;

    // The governor's target (xr_display.c depthGovernorWindow) is the rate the
    // model holds to
    if (ctx->depthGov.target != d->rateSet) {
        LOGI("depth rate: %d maps/s to the model (was %d)", ctx->depthGov.target, d->rateSet);
        d->ops->setRate(d->engine, ctx->depthGov.target);
        d->rateSet = ctx->depthGov.target;
    }
    if (fabsf(ctx->convergence - d->convSet) > 1e-4f) {
        MlxrDepthParams p;
        p.tauMs = DEPTH_TAU_DEFAULT_MS;
        p.rangeTauMs = DEPTH_RANGE_TAU_DEFAULT_MS;
        p.cutLevel = 1;
        p.global = 1.0f;
        p.local = 1.0f;
        p.convergence = ctx->convergence;
        d->ops->setParams(d->engine, &p);
        d->convSet = ctx->convergence;
    }

    if (ctx->frameCmd != VK_NULL_HANDLE && ctx->stereoLive) {
        uploadMap(ctx, d);
    }

    // Back on and waiting flat: a map made since the switch has landed, or
    // the wait is over (native: "made since" is the map's submit time, so a
    // capture queued before the 3D went off does not count)
    if (ctx->stereoWaiting) {
        const DepthTex* t = &d->tex[d->readIndex];
        int fresh = d->texValid && t->submitNs >= ctx->stereoWaitNs;
        if (fresh || now - ctx->stereoWaitNs > STEREO_WAIT_NS) {
            ctx->stereoWaiting = 0;
            ctx->warpRedraw = 1;
            LOGI("3d warping again after %.0f ms%s", (now - ctx->stereoWaitNs) / 1e6,
                 fresh ? "" : " (no fresh map yet)");
        }
    }
}

// ---- per picture ---------------------------------------------------------------------

static void recordCapture(XrCtx* ctx, XrDepth* d, const MlxrVideo* video, uint64_t frameIndex, int64_t frameNs) {
    DepthPair* p = NULL;
    pthread_mutex_lock(&d->lock);
    for (int i = 0; i < DEPTH_PAIRS; i++) {
        if (d->pairs[i].state == PAIR_FREE) {
            p = &d->pairs[i];
            break;
        }
    }
    pthread_mutex_unlock(&d->lock);
    if (p == NULL) {
        // Upstream: with no pair free the frame loop skips, never waits
        d->skipsNoPair++;
        return;
    }
    VkCommandBuffer cmd = ctx->frameCmd;
    xrGpuTimeBegin(ctx, GPU_DOWNSCALE);
    xrVkImageTo(ctx, &d->downscale, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    MlxrTarget t;
    memset(&t, 0, sizeof(t));
    t.view = d->downscale.view;
    t.format = MLXR_PASS_FORMAT;
    t.rect.extent = (VkExtent2D){ (uint32_t)d->w, (uint32_t)d->h };
    mlxr_pass_downscale(ctx->passes, cmd, &t, video);
    xrVkImageTo(ctx, &d->downscale, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy region = { 0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 },
                                 { (uint32_t)d->w, (uint32_t)d->h, 1 } };
    vkCmdCopyImageToBuffer(cmd, d->downscale.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, p->hb.buf, 1, &region);
    VkBufferMemoryBarrier bb = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
    bb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    bb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    bb.srcQueueFamilyIndex = bb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bb.buffer = p->hb.buf;
    bb.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1, &bb, 0,
                         NULL);
    xrGpuTimeEnd(ctx, GPU_DOWNSCALE);
    pthread_mutex_lock(&d->lock);
    if (p->state != PAIR_FREE) {
        d->pairReuseBad++;
    }
    p->state = PAIR_RECORDED;
    p->value = ctx->gpuSerial;
    p->frameId = frameIndex;
    p->captureNs = frameNs;
    pthread_mutex_unlock(&d->lock);
    d->captures++;
}

void xrDepthPicture(XrCtx* ctx, const MlxrVideo* video, int newFrame, uint64_t frameIndex, int64_t frameNs,
                    XrDepthDraw* out) {
    memset(out, 0, sizeof(*out));
    out->eyes = 1;
    const float W = (float)video->width;
    float separation = ctx->separationCurrent;
    XrDepth* d = ctx->depth;
    VkCommandBuffer cmd = ctx->frameCmd;
    int model = ctx->stereoMode == DEPTH_MODE_MODEL;
    int ready = d != NULL && cmd != VK_NULL_HANDLE
            && (model ? d->state == MLXR_DEPTH_READY : d->gpuReady);

    // The capture for the model, by time since the last rather than by frame
    // count (upstream nativeDepthDue), only with the 3D on
    if (ready && model && ctx->stereoLive) {
        int due = 0;
        if (d->forceCapture) {
            due = 1;
            d->forceCapture = 0;
        }
        else if (newFrame) {
            due = d->ops->due(d->engine, nowMono());
        }
        if (due) {
            recordCapture(ctx, d, video, frameIndex, frameNs);
        }
    }

    // Both eyes warped, unless the 3D is off or has only just come back on
    // and is waiting for a fresh map (native: or no map has come yet). Then
    // the frame is drawn once, flat, and none of the depth passes run.
    int haveMap = ready && d->texValid;
    int warping = ctx->stereoMode != DEPTH_MODE_OFF && ctx->stereoLive && !ctx->stereoWaiting && haveMap;
    int upsampling = warping && model && ctx->upsampleEnabled;
    if (upsampling && !ensureUpsample(ctx, d, (int)video->width, (int)video->height)) {
        upsampling = 0;
    }
    int occluding = upsampling && ctx->occlusionEnabled && separation > 0.0f;
    VkImageView mapView = haveMap ? d->tex[d->readIndex].img.view : VK_NULL_HANDLE;
    if (haveMap && warping) {
        d->tex[d->readIndex].lastUse = ctx->gpuSerial;
    }
    int lowResW = ctx->upsampleWidth > 0 ? ctx->upsampleWidth : (int)video->width / 4;

    // Runs every video frame rather than only when new depth lands, which
    // also re-snaps a map a few frames old onto the colour edges of the frame
    // it is actually warping
    if (upsampling) {
        xrGpuTimeBegin(ctx, GPU_UPSAMPLE);
        xrVkImageTo(ctx, &d->upsample, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        MlxrTarget t;
        memset(&t, 0, sizeof(t));
        t.view = d->upsample.view;
        t.format = MLXR_PASS_FORMAT;
        t.rect.extent = (VkExtent2D){ (uint32_t)d->upsample.width, (uint32_t)d->upsample.height };
        MlxrUpsampleParams up;
        memset(&up, 0, sizeof(up));
        up.video = *video;
        up.depth = mapView;
        up.depthWidth = (uint32_t)d->w;
        up.depthHeight = (uint32_t)d->h;
        up.sigmaR = ctx->upsampleSigmaR;
        up.sharp = ctx->depthSharp;
        mlxr_pass_upsample(ctx->passes, cmd, &t, &up);
        xrVkImageTo(ctx, &d->upsample, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        xrGpuTimeEnd(ctx, GPU_UPSAMPLE);
        lowResW = d->upsample.width;
    }
    // Both eyes in one pass, since they search the same depth reads
    if (occluding) {
        xrGpuTimeBegin(ctx, GPU_OFFSET);
        xrVkImageTo(ctx, &d->offsets, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        MlxrTarget t;
        memset(&t, 0, sizeof(t));
        t.view = d->offsets.view;
        t.format = MLXR_PASS_FORMAT;
        t.rect.extent = (VkExtent2D){ (uint32_t)d->offsets.width, (uint32_t)d->offsets.height };
        MlxrOffsetParams op;
        memset(&op, 0, sizeof(op));
        op.depth = d->upsample.view;
        op.dispTexels = separation * (float)d->upsample.width;
        op.convergence = ctx->convergence;
        mlxr_pass_offset(ctx->passes, cmd, &t, &op);
        xrVkImageTo(ctx, &d->offsets, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        xrGpuTimeEnd(ctx, GPU_OFFSET);
    }

    // Mono is a single full width draw with zero disparity, which with the 3D
    // switched off lands in the left half of the double wide chain. Stereo
    // draws the left eye into the left half and the right eye into the right
    // half, with opposite disparity signs.
    int eyes = warping ? 2 : 1;
    out->eyes = eyes;
    out->upsampled = upsampling;
    out->occluded = occluding;
    for (int eye = 0; eye < eyes; eye++) {
        MlxrWarpParams* w = &out->eye[eye];
        // Either the raw map or the edge aware upsample of it; both carry
        // depth in alpha, so the warp does not care which it got
        w->depth = upsampling ? d->upsample.view : mapView;
        w->offsets = occluding ? d->offsets.view : VK_NULL_HANDLE;
        w->occlusion = occluding;
        w->convergence = ctx->convergence;
        w->dispTexels = separation * (float)lowResW;
        w->lowResWidth = (float)lowResW;
        // Half a texel of the frame, which the shifted sample is held inside,
        // or with the clamp off a whole frame outside either edge
        w->srcInset = ctx->srcInsetOn ? 0.5f / W : -1.0f;
        w->edgeFade = (float)ctx->edgeFadePx / W;
        // Mono never uses the depth it reads, so it keeps the single fetch
        w->depthCubic = ctx->depthCubic && eyes == 2;
        w->showDepth = ctx->depthDebug && mapView != VK_NULL_HANDLE;
        w->eye = eye;
        w->disparity = 0.0f;
        if (eyes == 2 && ctx->stereoMode != DEPTH_MODE_EYETEST) {
            w->disparity = eye == 0 ? separation : -separation;
        }
        w->barTest = ctx->stereoMode == DEPTH_MODE_SHIFTTEST;
        w->tint[0] = w->tint[1] = w->tint[2] = 1.0f;
        if (ctx->stereoMode == DEPTH_MODE_EYETEST) {
            // Half 0 red, half 1 blue
            w->tint[0] = eye == 0 ? 1.0f : 0.2f;
            w->tint[1] = 0.2f;
            w->tint[2] = eye == 0 ? 0.2f : 1.0f;
        }
    }

    // How far behind the picture the depth it is warped with is (upstream's
    // Depth age: from the frame the live map was captured from to now)
    if (warping && model && newFrame) {
        const DepthTex* t = &d->tex[d->readIndex];
        if (t->captureNs > 0 && t->captureNs >= d->backNs) {
            double ageMs = (nowMono() - t->captureNs) / 1e6;
            d->ageMsSum += ageMs;
            d->ageFramesSum += frameIndex >= t->frameId ? (double)(frameIndex - t->frameId) : 0.0;
            d->ageSamples++;
            if (ageMs > d->ageWorstMs) d->ageWorstMs = ageMs;
            d->lastAgeMs = d->lastAgeMs == 0.0f ? (float)ageMs : d->lastAgeMs * 0.95f + (float)ageMs * 0.05f;
        }
    }
}

void xrDepthRecordWarp(XrCtx* ctx, VkCommandBuffer cmd, const MlxrTarget* target, const MlxrWarpParams* common,
                       const XrDepthDraw* draw) {
    for (int eye = 0; eye < draw->eyes; eye++) {
        MlxrTarget t = *target;
        t.rect.offset.x += eye * (int32_t)target->rect.extent.width;
        const MlxrWarpParams* e = &draw->eye[eye];
        MlxrWarpParams w = *common;
        w.depth = e->depth;
        w.offsets = e->offsets;
        w.disparity = e->disparity;
        w.eye = e->eye;
        w.convergence = e->convergence;
        w.dispTexels = e->dispTexels;
        w.lowResWidth = e->lowResWidth;
        w.srcInset = e->srcInset;
        w.edgeFade = e->edgeFade;
        w.occlusion = e->occlusion;
        w.depthCubic = e->depthCubic;
        w.showDepth = e->showDepth;
        w.barTest = e->barTest;
        for (int c = 0; c < 3; c++) w.tint[c] = common->tint[c] * e->tint[c];
        mlxr_pass_warp(ctx->passes, cmd, &t, &w);
    }
}

void xrDepthSubmitted(XrCtx* ctx, uint64_t serial) {
    XrDepth* d = ctx->depth;
    if (d == NULL || !d->gpuReady) return;
    int any = 0;
    pthread_mutex_lock(&d->lock);
    for (int i = 0; i < DEPTH_PAIRS; i++) {
        if (d->pairs[i].state == PAIR_RECORDED && d->pairs[i].value <= serial) {
            d->pairs[i].state = PAIR_QUEUED;
            any = 1;
        }
    }
    if (any) pthread_cond_broadcast(&d->cond);
    pthread_mutex_unlock(&d->lock);
}

// ---- numbers -------------------------------------------------------------------------

void xrDepthGetInfo(XrCtx* ctx, XrDepthInfo* info) {
    memset(info, 0, sizeof(*info));
    info->engineState = -1;
    XrDepth* d = ctx->depth;
    if (d == NULL) return;
    info->engineState = d->state;
    info->mapW = d->w;
    info->mapH = d->h;
    info->captures = d->captures;
    info->skipsNoPair = d->skipsNoPair;
    info->uploads = d->uploads;
    info->uploadBlocked = d->uploadBlocked;
    info->lastFrameId = d->lastFrameId;
    info->frameIdBackwards = d->frameIdBackwards;
    info->readIndex = d->readIndex;
    info->rateSet = d->rateSet;
    info->ageMsAvg = d->ageSamples ? d->ageMsSum / d->ageSamples : 0.0;
    info->ageFramesAvg = d->ageSamples ? d->ageFramesSum / d->ageSamples : 0.0;
    pthread_mutex_lock(&d->lock);
    info->submitted = d->submitted;
    info->submitFailed = d->submitFailed;
    info->pairReuseBad = d->pairReuseBad;
    info->stageWaitMsAvg = d->stageRuns ? (float)(d->stageWaitMs / d->stageRuns) : 0.0f;
    info->submitMsAvg = d->stageRuns ? (float)(d->submitMs / d->stageRuns) : 0.0f;
    pthread_mutex_unlock(&d->lock);
    for (int i = 0; i < DEPTH_TEX_COUNT; i++) {
        info->depthView[i] = d->tex[i].img.view;
        info->depthImage[i] = d->tex[i].img.image;
    }
    if (d->state == MLXR_DEPTH_READY && d->engine != NULL) {
        MlxrDepthStats st;
        d->ops->stats(d->engine, &st);
        info->maps = st.maps;
        info->replaced = st.replaced;
        info->mapsPerSecond = st.mapsPerSecond;
        info->modelMsAvg = st.modelMsAvg;
        info->modelMsP95 = st.modelMsP95;
        info->latencyMsAvg = st.latencyMsAvg;
        info->loadMs = st.loadMs;
        info->warmupMs = st.warmupMs;
        info->threads = st.threads;
    }
    info->upsampleImage = d->upsample.image;
    info->offsetImage = d->offsets.image;
    info->downscaleImage = d->downscale.image;
}

static const char* patternName(int mode) {
    switch (mode) {
    case DEPTH_MODE_FLAT: return "flat";
    case DEPTH_MODE_RAMP: return "ramp";
    case DEPTH_MODE_BLOB: return "blob";
    case DEPTH_MODE_EYETEST: return "eye test";
    case DEPTH_MODE_SHIFTTEST: return "shift test";
    default: return "?";
    }
}

void xrDepthStatsLine(XrCtx* ctx, char* buf, int len) {
    buf[0] = '\0';
    XrDepth* d = ctx->depth;
    if (ctx->stereoMode == DEPTH_MODE_OFF) {
        if (ctx->depthFailed) snprintf(buf, (size_t)len, "3d off (depth model failed)");
        return;
    }
    if (ctx->stereoMode != DEPTH_MODE_MODEL) {
        snprintf(buf, (size_t)len, "3d %s, test pattern %s, %d eyes drawn", ctx->stereoLive ? "on" : "off",
                 patternName(ctx->stereoMode), ctx->drawnEyes);
        return;
    }
    if (d == NULL || d->state == DEPTH_IDLE) {
        snprintf(buf, (size_t)len, "3d %s, depth model not started", ctx->stereoLive ? "on" : "off");
        return;
    }
    if (d->state == MLXR_DEPTH_LOADING) {
        snprintf(buf, (size_t)len, "3d %s, depth model %s loading", ctx->stereoLive ? "on" : "off", d->modelName);
        return;
    }
    MlxrDepthStats s;
    d->ops->stats(d->engine, &s);
    d->lastStats = s;
    uint64_t replaced = s.replaced - d->winReplaced;
    uint64_t skips = d->skipsNoPair - d->winSkips;
    d->winReplaced = s.replaced;
    d->winSkips = d->skipsNoPair;
    if (!ctx->stereoLive) {
        snprintf(buf, (size_t)len, "3d off, depth model idle (%llu maps so far)", (unsigned long long)s.maps);
        return;
    }
    double age = d->ageSamples ? d->ageMsSum / d->ageSamples : 0.0;
    double ageFrames = d->ageSamples ? d->ageFramesSum / d->ageSamples : 0.0;
    pthread_mutex_lock(&d->lock);
    float stage = d->stageRuns ? (float)((d->stageWaitMs + d->submitMs) / d->stageRuns) : 0.0f;
    pthread_mutex_unlock(&d->lock);
    snprintf(buf, (size_t)len,
             "3d on%s, %s %.1f maps/s (target %d), model %.1f ms (p95 %.1f), stage %.1f ms, capture to display "
             "%.0f ms avg (%.1f frames, worst %.0f), skipped %llu (no buffer %llu, replaced %llu), %d eyes drawn",
             ctx->stereoWaiting ? " (waiting)" : "", d->modelName, s.mapsPerSecond, ctx->depthGov.target,
             s.modelMsAvg, s.modelMsP95, stage, age, ageFrames, d->ageWorstMs,
             (unsigned long long)(skips + replaced), (unsigned long long)skips, (unsigned long long)replaced,
             ctx->drawnEyes);
    d->ageMsSum = d->ageFramesSum = d->ageWorstMs = 0.0;
    d->ageSamples = 0;
}

void xrDepthOverlay(XrCtx* ctx, char* buf, int len) {
    buf[0] = '\0';
    XrDepth* d = ctx->depth;
    if (ctx->stereoMode == DEPTH_MODE_OFF) {
        return;
    }
    if (ctx->stereoMode != DEPTH_MODE_MODEL) {
        snprintf(buf, (size_t)len, "3D: %s, test pattern %s", ctx->stereoLive ? "on" : "off",
                 patternName(ctx->stereoMode));
        return;
    }
    // Switched off from the bar or the 3D tab, the model's numbers are the
    // last ones it had and mean nothing, so they make way for saying so
    if (!ctx->stereoLive) {
        snprintf(buf, (size_t)len, "%s", d != NULL && d->state == MLXR_DEPTH_READY ? "3D: off, depth model idle"
                                                                                   : "3D: off");
        return;
    }
    if (d == NULL || d->state != MLXR_DEPTH_READY) {
        snprintf(buf, (size_t)len, "%s", ui_str(UIS_NATIVE_3D_LOADING));
        return;
    }
    MlxrDepthStats s;
    d->ops->stats(d->engine, &s);
    snprintf(buf, (size_t)len, "Depth %.1f/s (target %d), %s\nDepth inference %.1f ms, age %.0f ms, skipped %llu",
             s.mapsPerSecond, ctx->depthGov.target, d->modelName, s.modelMsAvg, d->lastAgeMs,
             (unsigned long long)(d->skipsNoPair + s.replaced));
}

void xrDepthDestroy(XrCtx* ctx) {
    XrDepth* d = ctx->depth;
    if (d == NULL) return;
    engineStop(ctx, d);
    if (ctx->vk != NULL && ctx->vk->device != VK_NULL_HANDLE) {
        freeGpu(ctx, d);
    }
    pthread_mutex_destroy(&d->lock);
    pthread_cond_destroy(&d->cond);
    free(d);
    ctx->depth = NULL;
}
