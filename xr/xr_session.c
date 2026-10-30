// Moonlight XR Native: the OpenXR session host. GPLv3.
//
// Upstream's xr_session.c and XrRenderer.java's frame loop, natively: one
// instance, session and Vulkan device for the life of the app; each frame
// polls events (the session states, a recentre, a refresh rate change), runs
// the ported input pass (xr_input.c, xrUpdateInput), hands what it decided to
// the app (the host mouse, the keyboard, the launcher's presses) and to
// config.ini, redraws the panels that changed into the UI atlas, draws a new
// video frame (the M2 hardware path: V4L2 Iris -> cached dma-buf import ->
// mlxr_pass_warp, now with the picture grade and the sharpening; or the
// software path through the same pass), and submits the layers xr_layers.c
// assembles.
//
// Device facts this follows (docs/probe/SUMMARY.md): SteamVR takes a Vulkan
// 1.2 instance; it offers one refresh rate and picks it; 16 swapchains a
// session; no cylinder, FB passthrough, layer settings or colour scale layers;
// ALPHA_BLEND is passthrough with a transparent projection layer under it.
#include "xr_session.h"

#include <libavutil/frame.h>
#include <signal.h>


#include "../shaders/passes.h"
#include "../audio/click.h"
#include "../stream/frame_slot.h"
#include "../stream/hw_video.h"
#include "../stream/pacer.h"
#include "../video/vk_import.h"
#include "mlxr_log.h"
#include "xr_native.h"
#include "xr_vk.h"
#include "xr_world.h"
#include "xr_depth.h"
#include "xr_battery.h"
#include "xr_perflog.h"
#include <dirent.h>

// Native: the GPU time each Vulkan device in this process used since the last
// call, from the kernel's per client counters (/proc/self/fdinfo, drm-engine-gpu).
// Ours is one device; SteamVR's client library opens its own in our process,
// and any work it does on our frames (such as downsampling a projection layer
// too large for the compositor) shows on it. For the room-judder diagnosis.
#define GPU_CLIENTS_MAX 8
static struct { unsigned long long id, ns; } g_gpuClients[GPU_CLIENTS_MAX];
static int g_gpuClientCount;

static void gpuByDeviceLine(char* buf, size_t size, double seconds) {
    buf[0] = 0;
    DIR* d = opendir("/proc/self/fd");
    if (d == NULL) {
        snprintf(buf, size, "unknown");
        return;
    }
    unsigned long long seen[GPU_CLIENTS_MAX];
    int nSeen = 0;
    size_t used = 0;
    struct dirent* e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        char path[64], target[64];
        snprintf(path, sizeof(path), "/proc/self/fd/%s", e->d_name);
        ssize_t n = readlink(path, target, sizeof(target) - 1);
        if (n <= 0) continue;
        target[n] = 0;
        if (strncmp(target, "/dev/dri/", 9) != 0) continue;
        snprintf(path, sizeof(path), "/proc/self/fdinfo/%s", e->d_name);
        FILE* f = fopen(path, "r");
        if (f == NULL) continue;
        char line[128];
        unsigned long long id = 0, ns = 0;
        int haveId = 0, haveNs = 0;
        while (fgets(line, sizeof(line), f)) {
            if (sscanf(line, "drm-client-id: %llu", &id) == 1) haveId = 1;
            else if (sscanf(line, "drm-engine-gpu: %llu", &ns) == 1) haveNs = 1;
        }
        fclose(f);
        if (!haveId || !haveNs) continue;
        int dup = 0;
        for (int i = 0; i < nSeen; i++) dup |= seen[i] == id;
        if (dup || nSeen >= GPU_CLIENTS_MAX) continue;
        seen[nSeen++] = id;
        int slot = -1;
        for (int i = 0; i < g_gpuClientCount; i++) {
            if (g_gpuClients[i].id == id) slot = i;
        }
        unsigned long long was = slot >= 0 ? g_gpuClients[slot].ns : ns;
        if (slot < 0 && g_gpuClientCount < GPU_CLIENTS_MAX) slot = g_gpuClientCount++;
        if (slot >= 0) {
            g_gpuClients[slot].id = id;
            g_gpuClients[slot].ns = ns;
        }
        double msPerS = seconds > 0.0 && ns >= was ? (double)(ns - was) / 1e6 / seconds : 0.0;
        if (used < size) {
            int w = snprintf(buf + used, size - used, "%sclient %llu %.1f ms/s", used ? ", " : "", id, msPerS);
            if (w > 0) used += (size_t)w;
        }
    }
    closedir(d);
    if (!buf[0]) snprintf(buf, size, "none");
}

static atomic_int g_quit;
void xr_app_request_quit(void) { atomic_store(&g_quit, 1); }

#define VK_OK(call, what) ((call) == VK_SUCCESS ? 1 : (LOGE("%s failed", what), 0))

// ---- cross thread requests ----------------------------------------------------

static pthread_mutex_t g_reqLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_reqDone = PTHREAD_COND_INITIALIZER;
static int g_running;          // a frame loop is up to take requests
static int g_releaseAsked;     // xr_app_release_video waiting
static unsigned g_releaseSerial, g_releaseDone;
static atomic_int g_streaming;
static char g_notices[4][160];
static int g_noticeCount;

void xr_app_notice(const char* text) {
    pthread_mutex_lock(&g_reqLock);
    if (g_noticeCount < 4) {
        snprintf(g_notices[g_noticeCount++], sizeof(g_notices[0]), "%s", text);
    }
    pthread_mutex_unlock(&g_reqLock);
}

void xr_app_set_streaming(int on) { atomic_store(&g_streaming, on); }

// Native (lane wave2-bind-wire): the host app's name for the bindings, taken
// up by the frame loop when the serial moves
static char g_hostApp[XR_BIND_APP_MAX];
static unsigned g_hostAppSerial;

void xr_app_set_host_app(const char* name) {
    pthread_mutex_lock(&g_reqLock);
    snprintf(g_hostApp, sizeof(g_hostApp), "%s", name != NULL ? name : "");
    g_hostAppSerial++;
    pthread_mutex_unlock(&g_reqLock);
}

static atomic_int g_kbToggle;

void xr_app_toggle_keyboard(void) { atomic_store(&g_kbToggle, 1); }

// The frame loop's side: the bindings follow a new host app, and a stream
// starting reads bindings.ini again, so a change made in the launcher's editor
// counts from the next stream
static void takeHostApp(XrCtx* ctx, unsigned* seen) {
    char name[XR_BIND_APP_MAX];
    pthread_mutex_lock(&g_reqLock);
    int fresh = g_hostAppSerial != *seen;
    *seen = g_hostAppSerial;
    snprintf(name, sizeof(name), "%s", g_hostApp);
    pthread_mutex_unlock(&g_reqLock);
    if (fresh) {
        if (name[0] != '\0') {
            xrBindLoad(ctx, NULL);
        }
        xrBindSetApp(ctx, name);
    }
}

void xr_app_release_video(void) {
    pthread_mutex_lock(&g_reqLock);
    if (!g_running) {
        pthread_mutex_unlock(&g_reqLock);
        return;
    }
    unsigned mine = ++g_releaseSerial;
    g_releaseAsked = 1;
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += 5;
    while (g_running && g_releaseDone < mine) {
        if (pthread_cond_timedwait(&g_reqDone, &g_reqLock, &deadline) != 0) {
            MLOG_W("video release: the frame loop did not answer in 5 s");
            break;
        }
    }
    pthread_mutex_unlock(&g_reqLock);
}

// ---- the picture (M1/M2 video path) -------------------------------------------

typedef struct Plane {
    VkImage image;
    VkDeviceMemory mem;
    VkImageView view;
} Plane;

#define VIDEO_IMAGES 8
#define HW_VIEWS 32

typedef struct Video {
    int width, height;          // the swapchain's size: the picture's, double wide with a stereo mode (M6)
    int picW;                   // the picture's width
    XrSwapchain swapchain;
    uint32_t count;
    XrSwapchainImageVulkan2KHR images[VIDEO_IMAGES];
    VkImageView views[VIDEO_IMAGES];
    int64_t format;
    // software frames: three planes and their staging
    Plane planes[3];
    int planeW, planeH;
    VkBuffer staging;
    VkDeviceMemory stagingMem;
    void* stagingPtr;
    VkDeviceSize stagingSize;
    int swValid;                // the planes hold a frame (a redraw can use them)
    // hardware frames
    HwVideo* src;
    mlxr_vkimp* imp;
    struct { uint32_t gen, index; VkImageView view; } hwViews[HW_VIEWS];
    mlxr_vframe held;           // the newest frame, kept for a redraw
    int heldValid;
    uint64_t heldValue;         // the timeline value of the last submission that sampled it
    VkSemaphore timeline;
    uint64_t value;
    // what the last draw used, so a change redraws a still picture
    int drawnGradeOn, drawnSharpen, drawnCurved;
    PictureGrade drawnGrade;
    // M6: the 3D as last drawn (eyes, separation, convergence, depth view)
    int drawnEyes, drawnShowDepth;
    float drawnSep, drawnConv;
    uint64_t draws;
} Video;

static int makePlane(const XrVk* vk, Plane* p, int w, int h) {
    VkImageCreateInfo ci = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = VK_FORMAT_R8_UNORM;
    ci.extent = (VkExtent3D){ (uint32_t)w, (uint32_t)h, 1 };
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (vkCreateImage(vk->device, &ci, NULL, &p->image) != VK_SUCCESS) return -1;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(vk->device, p->image, &req);
    VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = xr_vk_memory_type(vk, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(vk->device, &ai, NULL, &p->mem) != VK_SUCCESS) return -1;
    vkBindImageMemory(vk->device, p->image, p->mem, 0);
    VkImageViewCreateInfo vi = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    vi.image = p->image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R8_UNORM;
    vi.subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    return vkCreateImageView(vk->device, &vi, NULL, &p->view) == VK_SUCCESS ? 0 : -1;
}

static void freePlane(const XrVk* vk, Plane* p) {
    if (p->view) vkDestroyImageView(vk->device, p->view, NULL);
    if (p->image) vkDestroyImage(vk->device, p->image, NULL);
    if (p->mem) vkFreeMemory(vk->device, p->mem, NULL);
    memset(p, 0, sizeof(*p));
}

static void freeSwPlanes(const XrVk* vk, Video* vd) {
    for (int i = 0; i < 3; i++) freePlane(vk, &vd->planes[i]);
    if (vd->staging) {
        vkUnmapMemory(vk->device, vd->stagingMem);
        vkDestroyBuffer(vk->device, vd->staging, NULL);
        vkFreeMemory(vk->device, vd->stagingMem, NULL);
    }
    vd->staging = VK_NULL_HANDLE;
    vd->stagingMem = VK_NULL_HANDLE;
    vd->stagingSize = 0;
    vd->planeW = vd->planeH = 0;
    vd->swValid = 0;
}

static void freeVideoSwapchain(XrCtx* ctx, Video* vd) {
    for (uint32_t i = 0; i < vd->count; i++) vkDestroyImageView(ctx->vk->device, vd->views[i], NULL);
    vd->count = 0;
    if (vd->swapchain) {
        xrDestroySwapchain(vd->swapchain);
        ctx->swapchainsAlive--;
    }
    vd->swapchain = XR_NULL_HANDLE;
    ctx->swapchain = XR_NULL_HANDLE;
    ctx->videoShown = 0;
}

// The picture's swapchain at the picture's size, double wide with a stereo
// mode (upstream's chainWidth: each eye's half, or the left one flat).
// Native (lane eye-swapchains): with a stereo mode SteamVR is never shown this
// one (xrPictureProjected: the picture goes up through the curved picture's
// projection layer, an image per eye), so its width past 3458 costs memory
// but no downsampling. Native (PLAN 4.3): the picture is what one eye sees of
// the decoded frame, fw x fh, which is half of a full size host-rendered pair.
static int videoSwapchain(XrCtx* ctx, Video* vd, int fw, int fh) {
    int pw, h;
    xrDepthPictureSize(ctx, fw, fh, &pw, &h);
    int w = xrDepthChainWidth(ctx, pw);
    if (vd->swapchain && vd->width == w && vd->picW == pw && vd->height == h) {
        return 0;
    }
    vkDeviceWaitIdle(ctx->vk->device);
    freeVideoSwapchain(ctx, vd);
    XrSwapchainCreateInfo ci = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
    ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    ci.format = vd->format;
    ci.sampleCount = 1;
    ci.width = (uint32_t)w;
    ci.height = (uint32_t)h;
    ci.faceCount = ci.arraySize = ci.mipCount = 1;
    if (!checkXr(xrCreateSwapchain(ctx->session, &ci, &vd->swapchain), "create video swapchain")) return -1;
    ctx->swapchainsAlive++;
    xrEnumerateSwapchainImages(vd->swapchain, 0, &vd->count, NULL);
    if (vd->count > VIDEO_IMAGES) vd->count = VIDEO_IMAGES;
    for (uint32_t i = 0; i < vd->count; i++) vd->images[i] = (XrSwapchainImageVulkan2KHR){ XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR };
    xrEnumerateSwapchainImages(vd->swapchain, vd->count, &vd->count, (XrSwapchainImageBaseHeader*)vd->images);
    for (uint32_t i = 0; i < vd->count; i++) {
        VkImageViewCreateInfo vi = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        vi.image = vd->images[i].image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = (VkFormat)vd->format;
        vi.subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        if (vkCreateImageView(ctx->vk->device, &vi, NULL, &vd->views[i]) != VK_SUCCESS) return -1;
    }
    vd->width = w;
    vd->picW = pw;
    vd->height = h;
    ctx->swapchain = vd->swapchain;
    // The picture's shape follows the stream's
    ctx->videoWidth = pw;
    ctx->videoHeight = h;
    ctx->pointerBeta = nativePointerTunable("MLXR_POINTER_BETA", NATIVE_POINTER_BETA_PER_PX) * (float)pw;
    LOGEV("video swapchain %dx%d (%s), %u images; swapchains alive %d of 16", w, h,
          w != pw ? "double wide, stereo" : "flat", vd->count, ctx->swapchainsAlive);
    return 0;
}

static int swPlanes(const XrVk* vk, Video* vd, const AVFrame* f) {
    if (vd->staging && vd->planeW == f->width && vd->planeH == f->height) {
        return 0;
    }
    vkDeviceWaitIdle(vk->device);
    freeSwPlanes(vk, vd);
    int cw = (f->width + 1) / 2, ch = (f->height + 1) / 2;
    if (makePlane(vk, &vd->planes[0], f->width, f->height) || makePlane(vk, &vd->planes[1], cw, ch)
            || makePlane(vk, &vd->planes[2], cw, ch)) {
        return -1;
    }
    vd->stagingSize = (VkDeviceSize)f->linesize[0] * f->height + (VkDeviceSize)f->linesize[1] * ch
            + (VkDeviceSize)f->linesize[2] * ch + 1024;
    VkBufferCreateInfo bi = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bi.size = vd->stagingSize;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (vkCreateBuffer(vk->device, &bi, NULL, &vd->staging) != VK_SUCCESS) return -1;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(vk->device, vd->staging, &req);
    VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = xr_vk_memory_type(vk, req.memoryTypeBits,
                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(vk->device, &ai, NULL, &vd->stagingMem) != VK_SUCCESS) return -1;
    vkBindBufferMemory(vk->device, vd->staging, vd->stagingMem, 0);
    vkMapMemory(vk->device, vd->stagingMem, 0, VK_WHOLE_SIZE, 0, &vd->stagingPtr);
    vd->planeW = f->width;
    vd->planeH = f->height;
    MLOG_I("software video planes %dx%d", f->width, f->height);
    return 0;
}

// Copies a software frame into the planes (recorded into cmd)
static void swUpload(Video* vd, VkCommandBuffer cmd, const AVFrame* f) {
    int ch = (f->height + 1) / 2, cw = (f->width + 1) / 2;
    int hs[3] = { f->height, ch, ch }, ws[3] = { f->width, cw, cw };
    VkDeviceSize off = 0;
    for (int p = 0; p < 3; p++) {
        size_t bytes = (size_t)f->linesize[p] * hs[p];
        memcpy((uint8_t*)vd->stagingPtr + off, f->data[p], bytes);
        VkBufferImageCopy region = { off, (uint32_t)f->linesize[p], 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                                     { 0, 0, 0 }, { (uint32_t)ws[p], (uint32_t)hs[p], 1 } };
        VkImageLayout from = vd->swValid ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
        mlxr_barrier(cmd, vd->planes[p].image, VK_IMAGE_ASPECT_COLOR_BIT, from, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        vkCmdCopyBufferToImage(cmd, vd->staging, vd->planes[p].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        mlxr_barrier(cmd, vd->planes[p].image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        off += (bytes + 255) & ~(VkDeviceSize)255;
    }
    vd->swValid = 1;
}

static void hwDropViews(const XrVk* vk, Video* vd) {
    for (int i = 0; i < HW_VIEWS; i++)
        if (vd->hwViews[i].view) vkDestroyImageView(vk->device, vd->hwViews[i].view, NULL);
    memset(vd->hwViews, 0, sizeof(vd->hwViews));
}

// Gives the held frame back once the GPU is done with its last sampling
static void hwLetGoHeld(Video* vd) {
    if (vd->heldValid && vd->imp) {
        mlxr_vkimp_frame_done_timeline(vd->imp, &vd->held, vd->timeline, vd->heldValue);
    }
    vd->heldValid = 0;
}

// Drops every import (GPU idle first). The frames go back to the decoder.
static void hwDropImports(const XrVk* vk, Video* vd) {
    if (!vd->imp) return;
    vkDeviceWaitIdle(vk->device);
    hwLetGoHeld(vd);
    mlxr_vkimp_drain(vd->imp, 1000000000ull);
    hwDropViews(vk, vd);
    mlxr_vkimp_reset(vd->imp);
}

static void hwDestroy(const XrVk* vk, Video* vd) {
    if (vd->imp) {
        hwDropImports(vk, vd);
        mlxr_vkimp_destroy(vd->imp);
        vd->imp = NULL;
    }
    vd->src = NULL;
}

static void hwReleaseCb(void* user, const mlxr_vframe* f) { hw_video_release((HwVideo*)user, f); }

static VkImageView hwView(const XrVk* vk, Video* vd, MlxrPasses* passes, const mlxr_vkimp_image* img) {
    int slot = -1;
    for (int i = 0; i < HW_VIEWS; i++) {
        if (vd->hwViews[i].view && vd->hwViews[i].gen == img->generation && vd->hwViews[i].index == img->index)
            return vd->hwViews[i].view;
        if (!vd->hwViews[i].view && slot < 0) slot = i;
    }
    if (slot < 0) {
        hwDropViews(vk, vd);
        slot = 0;
    }
    VkImageView v = VK_NULL_HANDLE;
    if (mlxr_passes_nv12_view(passes, img->image, &v) != VK_SUCCESS) return VK_NULL_HANDLE;
    vd->hwViews[slot].gen = img->generation;
    vd->hwViews[slot].index = img->index;
    vd->hwViews[slot].view = v;
    return v;
}

// ---- the curved picture -------------------------------------------------------------
// SteamVR has no cylinder layers, so a curved picture is drawn by the app:
// the warp goes into an image of the app's own (a swapchain image cannot be
// sampled once released), and every display frame mlxr_pass_panel draws that
// on upstream's cylinder into a projection layer for both eyes.
//
// Native (lane eye-swapchains): the projection layer has a swapchain per eye,
// each the runtime's recommended eye size times MLXR_EYE_SCALE. Both eyes side
// by side (2 x 2160 = 4320 wide at the Frame's 2160) went past the 3458 px
// SteamVR's client library takes as it is, so it downsampled the layer every
// frame and the picture came out at about 1729 px per eye whatever the
// resolution setting. In 3D the warp's own image is one per eye as well.

#define CURVE_IMAGES 8
#define CURVE_PICTURE_FORMAT VK_FORMAT_R8G8B8A8_UNORM
#define CURVE_PICTURES 2

// One of the warp's own images, with its mip chain
typedef struct CurvePicture {
    VkImage image;
    VkDeviceMemory mem;
    VkImageView view;          // every mip level: what the panel pass samples
    VkImageView rtView;        // level 0: what the warp draws into
    VkImageLayout layout;
} CurvePicture;

typedef struct XrCurve {
    // Native (lane eye-swapchains): an image per eye with a stereo chain (each
    // the picture's size), else pic[0] alone
    CurvePicture pic[CURVE_PICTURES];
    int pics;
    uint32_t mips;
    int w, h;                  // each image's
    int valid;                 // a frame has been warped into it
    // Native (lane eye-swapchains): the projection layer's swapchain per eye
    XrSwapchain swapchain[ROOM_EYES];
    uint32_t count[ROOM_EYES];
    XrSwapchainImageVulkan2KHR images[ROOM_EYES][CURVE_IMAGES];
    VkImageView views[ROOM_EYES][CURVE_IMAGES];
    int eyeW, eyeH;
    int failed;
    int layerSaid;
    // What a warp into this image counts as for videoStale: 1 the curve, 2 + 4
    // x width the supersampled picture (M5)
    int mode;
    XrCompositionLayerProjection layer;
    XrCompositionLayerProjectionView eyes[2];
} XrCurve;

static void curveFreePicture(const XrVk* vk, XrCurve* cv) {
    for (int i = 0; i < CURVE_PICTURES; i++) {
        CurvePicture* p = &cv->pic[i];
        if (p->view) vkDestroyImageView(vk->device, p->view, NULL);
        if (p->rtView) vkDestroyImageView(vk->device, p->rtView, NULL);
        if (p->image) vkDestroyImage(vk->device, p->image, NULL);
        if (p->mem) vkFreeMemory(vk->device, p->mem, NULL);
        memset(p, 0, sizeof(*p));
    }
    cv->pics = 0;
    cv->w = cv->h = 0;
    cv->valid = 0;
}

static void curveDestroy(XrCtx* ctx, XrCurve* cv) {
    curveFreePicture(ctx->vk, cv);
    for (int e = 0; e < ROOM_EYES; e++) {
        for (uint32_t i = 0; i < cv->count[e]; i++) vkDestroyImageView(ctx->vk->device, cv->views[e][i], NULL);
        cv->count[e] = 0;
        if (cv->swapchain[e]) {
            xrDestroySwapchain(cv->swapchain[e]);
            ctx->swapchainsAlive--;
        }
        cv->swapchain[e] = XR_NULL_HANDLE;
    }
}

static int curvePictureMake(const XrVk* vk, CurvePicture* p, int w, int h, uint32_t mips) {
    VkImageCreateInfo ci = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = CURVE_PICTURE_FORMAT;
    ci.extent = (VkExtent3D){ (uint32_t)w, (uint32_t)h, 1 };
    ci.mipLevels = mips;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
             | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (vkCreateImage(vk->device, &ci, NULL, &p->image) != VK_SUCCESS) return -1;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(vk->device, p->image, &req);
    VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = xr_vk_memory_type(vk, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(vk->device, &ai, NULL, &p->mem) != VK_SUCCESS) return -1;
    vkBindImageMemory(vk->device, p->image, p->mem, 0);
    VkImageViewCreateInfo vi = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    vi.image = p->image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = CURVE_PICTURE_FORMAT;
    vi.subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, mips, 0, 1 };
    if (vkCreateImageView(vk->device, &vi, NULL, &p->view) != VK_SUCCESS) return -1;
    vi.subresourceRange.levelCount = 1;
    if (vkCreateImageView(vk->device, &vi, NULL, &p->rtView) != VK_SUCCESS) return -1;
    p->layout = VK_IMAGE_LAYOUT_UNDEFINED;
    return 0;
}

// The app's own copy of the picture. Native M5 fix: _UNORM holding the gamma
// encoded bytes the warp computes (OUT_LINEAR off), since the panel pass that
// samples it treats what it reads as gamma encoded and decodes it itself for
// the _SRGB target; through an _SRGB view the sampler had decoded it once
// already, and the curved picture came out darker than the flat one.
// Native (lane eye-swapchains): pics images of w x h, one per eye in 3D.
static int curvePicture(XrCtx* ctx, XrCurve* cv, int w, int h, int pics) {
    if (cv->pics == pics && cv->w == w && cv->h == h) {
        return 0;
    }
    const XrVk* vk = ctx->vk;
    vkDeviceWaitIdle(vk->device);
    curveFreePicture(vk, cv);
    // Native: a full mip chain, made after every warp (curveMips), so the
    // panel pass shrinking a 2560 wide picture to the few hundred pixels it
    // covers in an eye filters it rather than skipping texels (text crawled
    // and broke up as the head moved, owner 2026-10-10)
    uint32_t mips = 1;
    for (int m = w > h ? w : h; m > 1; m >>= 1) mips++;
    for (int i = 0; i < pics; i++) {
        if (curvePictureMake(vk, &cv->pic[i], w, h, mips) != 0) {
            curveFreePicture(vk, cv);
            return -1;
        }
    }
    cv->pics = pics;
    cv->mips = mips;
    cv->w = w;
    cv->h = h;
    LOGEV("curved picture: own image %dx%d%s", w, h, pics > 1 ? ", one per eye" : "");
    return 0;
}

// The projection layer's swapchains, one per eye (native, lane eye-swapchains)
static int curveSwapchain(XrCtx* ctx, XrCurve* cv, int64_t format) {
    if (cv->swapchain[0] && cv->swapchain[1]) return 0;
    if (cv->failed) return -1;
    float scale = eyeScaleParse(getenv("MLXR_EYE_SCALE"));
    int within = pictureEyeSize(ctx->recommendedEyeWidth, ctx->recommendedEyeHeight, ctx->maxEyeWidth,
                                ctx->maxEyeHeight, scale, &cv->eyeW, &cv->eyeH);
    for (int e = 0; e < ROOM_EYES; e++) {
        XrSwapchainCreateInfo ci = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
        ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
        ci.format = format;
        ci.sampleCount = 1;
        ci.width = (uint32_t)cv->eyeW;
        ci.height = (uint32_t)cv->eyeH;
        ci.faceCount = ci.arraySize = ci.mipCount = 1;
        if (!checkXr(xrCreateSwapchain(ctx->session, &ci, &cv->swapchain[e]), "create curved picture swapchain")) {
            cv->failed = 1;
            return -1;
        }
        ctx->swapchainsAlive++;
        xrEnumerateSwapchainImages(cv->swapchain[e], 0, &cv->count[e], NULL);
        if (cv->count[e] > CURVE_IMAGES) cv->count[e] = CURVE_IMAGES;
        for (uint32_t i = 0; i < cv->count[e]; i++) {
            cv->images[e][i] = (XrSwapchainImageVulkan2KHR){ XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR };
        }
        xrEnumerateSwapchainImages(cv->swapchain[e], cv->count[e], &cv->count[e],
                                   (XrSwapchainImageBaseHeader*)cv->images[e]);
        for (uint32_t i = 0; i < cv->count[e]; i++) {
            VkImageViewCreateInfo vi = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            vi.image = cv->images[e][i].image;
            vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vi.format = (VkFormat)format;
            vi.subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            if (vkCreateImageView(ctx->vk->device, &vi, NULL, &cv->views[e][i]) != VK_SUCCESS) {
                cv->failed = 1;
                return -1;
            }
        }
    }
    LOGEV("curved picture: projection layer %dx%d per eye, a swapchain each (recommended %dx%d, MLXR_EYE_SCALE "
          "%.2f), %u images each; %s SteamVR's %d; swapchains alive %d of 16", cv->eyeW, cv->eyeH,
          ctx->recommendedEyeWidth, ctx->recommendedEyeHeight, (double)scale, cv->count[0],
          within ? "within" : "past, so downsampled each frame by", STEAMVR_TEXTURE_MAX, ctx->swapchainsAlive);
    return 0;
}

// One display frame of the curved picture: both eyes drawn from where they
// are now. Fills the layer and returns the acquired images' release duty.
static int curveRecord(XrCtx* ctx, XrCurve* cv, MlxrPasses* passes, VkCommandBuffer cmd, int64_t format,
                       XrSpace space, int eyeSwap) {
    (void)passes;
    if (!cv->valid || curveSwapchain(ctx, cv, format) != 0) return 0;
    // Native: the room or the models drawn in here too (one projection layer a
    // frame, xrWorldPlanLayers) are world locked, so the layer is drawn in the
    // local space; a head locked picture is placed there from where the head
    // is at the frame's time
    int world = ctx->roomInPicture || ctx->modelsInPicture;
    XrPosef picturePose = ctx->screenPose;
    if (world && space != ctx->localSpace) {
        XrSpaceLocation head = { XR_TYPE_SPACE_LOCATION };
        XrSpaceLocationFlags both = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
        if (XR_FAILED(xrLocateSpace(space, ctx->localSpace, ctx->predictedDisplayTime, &head))
                || (head.locationFlags & both) != both) {
            return 0;
        }
        Vec3 at = quatRotate(head.pose.orientation, (Vec3){ picturePose.position.x, picturePose.position.y,
                                                             picturePose.position.z });
        picturePose.orientation = quatNorm(quatMul(head.pose.orientation, picturePose.orientation));
        picturePose.position.x = head.pose.position.x + at.x;
        picturePose.position.y = head.pose.position.y + at.y;
        picturePose.position.z = head.pose.position.z + at.z;
        space = ctx->localSpace;
    }
    XrViewLocateInfo li = { XR_TYPE_VIEW_LOCATE_INFO };
    li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    li.displayTime = ctx->predictedDisplayTime;
    li.space = space;
    XrViewState vs = { XR_TYPE_VIEW_STATE };
    XrView views[2] = { { XR_TYPE_VIEW }, { XR_TYPE_VIEW } };
    uint32_t n = 0;
    if (XR_FAILED(xrLocateViews(ctx->session, &li, &vs, 2, &n, views)) || n < 2
            || !(vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)
            || (world && !(vs.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT))) {
        return 0;
    }
    // Native (lane eye-swapchains): an image of each eye's swapchain
    uint32_t idx[ROOM_EYES] = { 0, 0 };
    for (int e = 0; e < ROOM_EYES; e++) {
        XrSwapchainImageAcquireInfo aq = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
        XrSwapchainImageWaitInfo wi = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
        wi.timeout = XR_INFINITE_DURATION;
        XrSwapchainImageReleaseInfo rl = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
        int ok = checkXr(xrAcquireSwapchainImage(cv->swapchain[e], &aq, &idx[e]), "acquire curved picture image");
        if (ok && !checkXr(xrWaitSwapchainImage(cv->swapchain[e], &wi), "wait curved picture image")) {
            xrReleaseSwapchainImage(cv->swapchain[e], &rl);
            ok = 0;
        }
        if (!ok) {
            if (e > 0) xrReleaseSwapchainImage(cv->swapchain[0], &rl);
            return 0;
        }
    }
    VkImage images[ROOM_EYES];
    VkImageView targets[ROOM_EYES];
    for (int e = 0; e < ROOM_EYES; e++) {
        images[e] = cv->images[e][idx[e]].image;
        targets[e] = cv->views[e][idx[e]];
    }
    xrGpuTimeBegin(ctx, GPU_CURVE);
    for (int e = 0; e < ROOM_EYES; e++) {
        mlxr_barrier(cmd, images[e], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    }
    XrEyeTargets tg = xrEyesPerImage(images, targets, (VkFormat)format, cv->eyeW, cv->eyeH);
    tg.clear = 1; // transparent round the picture: the void or the room shows
    // Native (lane wave3-visual): or, standing in for the backdrop's own layer,
    // the dimmed cameras' black or the void's colour. The colour is the one as
    // typed, so an _SRGB image is cleared to its linear value.
    float backdrop[4];
    if (ctx->passthroughInPicture && xrBackdropColor(ctx, ctx->passthrough, backdrop)) {
        int srgb = format == VK_FORMAT_R8G8B8A8_SRGB || format == VK_FORMAT_B8G8R8A8_SRGB;
        for (int i = 0; i < 3; i++) {
            tg.clearColor[i] = srgb ? srgbToLinear(backdrop[i]) : backdrop[i];
        }
        tg.clearColor[3] = backdrop[3];
    }
    // Native: the room first, as this layer's opaque background, and then the
    // glow and the picture over it in the order their layers had
    if (ctx->roomInPicture && xrRoomRecordInto(ctx, cmd, &tg, views)) {
        xrEyesBarrier(cmd, &tg);
        tg.clear = 0;
    }
    // The glow and the picture read the picture's pose off the context; a head
    // locked one put in the local space above stands in for it while they draw
    XrPosef heldPose = ctx->screenPose;
    ctx->screenPose = picturePose;
    // M5: the glow round a curved picture, upstream's glow cylinder drawn
    // into this layer under the picture (no cylinder layers on SteamVR)
    // Native (lane wave3-visual): feathered (the Picture tab's Glow edge) the glow
    // image is the picture's border fading into it, so it is drawn over the
    // picture, below, instead
    int glowOver = ctx->glowDrawnFeather > 0;
    if (xrWorldGlowShown(ctx) && !glowOver) {
        // Native: flat in 3D, the glow quad's image drawn here as well
        if (xrPictureCurved(ctx)) xrWorldCurvedGlow(ctx, &tg, views);
        else xrWorldFlatGlowInto(ctx, &tg, views);
        xrEyesBarrier(cmd, &tg);
        tg.clear = 0;
    }
    MlxrPanelParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.src = cv->pic[0].view;
    xrCurveShape(ctx, &pp.width, &pp.height, &pp.radius);
    pp.segments = 96;
    pp.color[0] = pp.color[1] = pp.color[2] = pp.color[3] = 1.0f;
    memset(&cv->layer, 0, sizeof(cv->layer));
    VkImageView src[ROOM_EYES];
    for (int e = 0; e < 2; e++) {
        xrCurveMvp(views[e].fov, views[e].pose, ctx->screenPose, pp.mvp[e]);
        // M6: in 3D each eye samples its own picture (upstream's cylinder
        // layers' subImage per eye); flat, both eyes the one. Native (lane
        // eye-swapchains): an image per eye in 3D, so no seam to keep clear of
        // (the double wide image needed SEAM_INSET_TEXELS at each side).
        int half = ctx->drawnEyes == 2 && cv->pics > 1 ? sbsEyeHalf(e, eyeSwap) : 0;
        src[e] = cv->pic[half].view;
        pp.uvRect[e][0] = 0.0f;
        pp.uvRect[e][1] = 0.0f;
        pp.uvRect[e][2] = cv->w > 0 ? (float)ctx->videoWidth / (float)cv->w : 1.0f;
        pp.uvRect[e][3] = 1.0f;
        // Native (lane eye-swapchains): each view its own swapchain's image
        xrProjectionEye(&cv->eyes[e], &views[e], cv->swapchain[e], cv->eyeW, cv->eyeH);
    }
    xrEyesPanel(ctx, cmd, &tg, &pp, src);
    if (xrWorldGlowShown(ctx) && glowOver) {
        xrEyesBarrier(cmd, &tg);
        tg.clear = 0;
        if (xrPictureCurved(ctx)) xrWorldCurvedGlow(ctx, &tg, views);
        else xrWorldFlatGlowInto(ctx, &tg, views);
    }
    xrGpuTimeEnd(ctx, GPU_CURVE);
    ctx->screenPose = heldPose;
    // Native: the models last, over the picture, as their own layer was
    if (ctx->modelsInPicture) {
        xrModelRecordOver(ctx, cmd, &tg, views);
    }
    cv->layer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
    // Opaque with the room behind the picture, as the room's own layer was
    cv->layer.layerFlags = ctx->roomInPicture ? 0 : XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    cv->layer.space = space;
    cv->layer.viewCount = 2;
    cv->layer.views = cv->eyes;
    if (!cv->layerSaid) {
        cv->layerSaid = 1;
        LOGEV("curved picture: the layer's two views on %s, %dx%d each", xrProjectionEyesOwn(&cv->layer)
              ? "two swapchains within SteamVR's 3458" : "a shared or oversized swapchain (downsampled)", cv->eyeW,
              cv->eyeH);
    }
    return 1;
}

// The sharpen the Display tab's row asks for: off, normal, quality
static float sharpenFor(int mode) {
    return mode == 2 ? 0.8f : (mode == 1 ? 0.4f : 0.0f);
}

// One level's layout change, with the stages a blit chain needs
static void mipBarrier(VkCommandBuffer cmd, VkImage image, uint32_t level, uint32_t count, VkImageLayout from,
                       VkImageLayout to, VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                       VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
    VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    b.srcAccessMask = srcAccess;
    b.dstAccessMask = dstAccess;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, level, count, 0, 1 };
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, NULL, 0, NULL, 1, &b);
}

// Native: the curved (or 3D) picture's mip chain from the level the warp just
// drew, each level a linear blit of the one above, all of it left for the
// panel pass to sample. Native (lane eye-swapchains): one image of them, i. Every level is in COLOR_ATTACHMENT_OPTIMAL on entry
// (the warp's barrier covers them all).
static void curveMips(VkCommandBuffer cmd, XrCurve* cv, int i) {
    VkImage image = cv->pic[i].image;
    const VkPipelineStageFlags COLOR = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    const VkPipelineStageFlags XFER = VK_PIPELINE_STAGE_TRANSFER_BIT;
    const VkPipelineStageFlags READ = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    mipBarrier(cmd, image, 0, 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, COLOR, XFER);
    int w = cv->w, h = cv->h;
    for (uint32_t m = 1; m < cv->mips; m++) {
        int nw = w > 1 ? w / 2 : 1, nh = h > 1 ? h / 2 : 1;
        // After the warp's own layout change of every level, before this write
        mipBarrier(cmd, image, m, 1, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   0, VK_ACCESS_TRANSFER_WRITE_BIT, COLOR | XFER, XFER);
        VkImageBlit blit;
        memset(&blit, 0, sizeof(blit));
        blit.srcSubresource = (VkImageSubresourceLayers){ VK_IMAGE_ASPECT_COLOR_BIT, m - 1, 0, 1 };
        blit.srcOffsets[1] = (VkOffset3D){ w, h, 1 };
        blit.dstSubresource = (VkImageSubresourceLayers){ VK_IMAGE_ASPECT_COLOR_BIT, m, 0, 1 };
        blit.dstOffsets[1] = (VkOffset3D){ nw, nh, 1 };
        vkCmdBlitImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        mipBarrier(cmd, image, m, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, XFER, XFER);
        w = nw;
        h = nh;
    }
    mipBarrier(cmd, image, 0, cv->mips, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT, XFER,
               READ);
}

// Records the warp of one picture into swapchain image idx (or the app's own
// image): the picture grade and the sharpening as the panel has them now, and
// M6's 3D (xr_depth.c: the capture for the model, the upsample, the offset
// search, then a warp per eye: native (lane eye-swapchains), into an image of
// its own for the curved or 3D picture, the halves of the double wide chain
// for the quad)
static void recordWarp(XrCtx* ctx, Video* vd, MlxrPasses* passes, VkCommandBuffer cmd, uint32_t idx,
                       const MlxrVideo* video, XrCurve* curve, int newFrame, uint64_t frameIndex) {
    (void)passes;
    XrDepthDraw dd;
    xrDepthPicture(ctx, video, newFrame, frameIndex, nowNs(), &dd);
    if (curve != NULL && dd.eyes > curve->pics) {
        // Never: only a stereo chain warps two eyes, and its own image is two
        dd.eyes = curve->pics;
    }
    xrGpuTimeBegin(ctx, GPU_WARP);
    // Eye 0's rect: the picture's size (native, PLAN 4.3: one eye's half of
    // a full size host-rendered pair, else the frame's)
    MlxrTarget tg[ROOM_EYES];
    memset(tg, 0, sizeof(tg));
    for (int e = 0; e < ROOM_EYES; e++) {
        tg[e].rect.extent = (VkExtent2D){ (uint32_t)vd->picW, (uint32_t)vd->height };
    }
    if (curve != NULL) {
        // The curved picture's own images, sampled afterwards by the panel pass
        for (int e = 0; e < dd.eyes; e++) {
            mlxr_barrier(cmd, curve->pic[e].image, VK_IMAGE_ASPECT_COLOR_BIT, curve->pic[e].layout,
                         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            tg[e].view = curve->pic[e].rtView;
            tg[e].format = CURVE_PICTURE_FORMAT;
        }
    }
    else {
        xr_vk_image_barrier(cmd, vd->images[idx].image, VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        // Eye 1 lands right of eye 0 in the double wide chain
        for (int e = 0; e < ROOM_EYES; e++) {
            tg[e].view = vd->views[idx];
            tg[e].format = (VkFormat)vd->format;
            tg[e].rect.offset.x = e * (int32_t)vd->picW;
        }
    }
    MlxrWarpParams w;
    memset(&w, 0, sizeof(w));
    w.video = *video;
    w.gradeOn = ctx->gradeOn;
    w.grade[0] = ctx->grade.offset;
    w.grade[1] = ctx->grade.contrast;
    w.grade[2] = ctx->grade.exponent;
    w.grade[3] = ctx->grade.saturation;
    w.tint[0] = w.tint[1] = w.tint[2] = 1.0f;
    w.sharpen = sharpenFor(ctx->sharpenMode);
    xrDepthRecordWarpEyes(ctx, cmd, tg, &w, &dd);
    if (curve != NULL) {
        for (int e = 0; e < dd.eyes; e++) {
            curveMips(cmd, curve, e);
            curve->pic[e].layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        curve->valid = 1;
    }
    // What the layers show from here on (upstream renderVideoFrame)
    ctx->drawnEyes = dd.eyes;
    ctx->warpRedraw = 0;
    vd->drawnCurved = curve != NULL ? curve->mode : 0;
    vd->drawnGradeOn = ctx->gradeOn;
    vd->drawnGrade = ctx->grade;
    vd->drawnSharpen = ctx->sharpenMode;
    vd->drawnEyes = dd.eyes;
    vd->drawnSep = ctx->separationCurrent;
    vd->drawnConv = ctx->convergence;
    vd->drawnShowDepth = dd.eye[0].showDepth;
    vd->draws++;
}

// Whether the grade, the sharpening or where the picture goes (flat, curved,
// supersampled at a size: drawnCurved's key) moved since it was drawn
static int videoStale(XrCtx* ctx, const Video* vd, int curved) {
    // M6: or the 3D switched (warpRedraw, upstream), or the 3D tab's depth
    // or convergence moved on a picture standing still
    int stereo3d = vd->drawnEyes == 2
            && (fabsf(vd->drawnSep - ctx->separationCurrent) > 1e-6f
                || fabsf(vd->drawnConv - ctx->convergence) > 1e-6f);
    return vd->draws > 0 && (vd->drawnGradeOn != ctx->gradeOn || vd->drawnSharpen != ctx->sharpenMode
                             || vd->drawnCurved != curved || ctx->warpRedraw || stereo3d
                             || memcmp(&vd->drawnGrade, &ctx->grade, sizeof(PictureGrade)) != 0);
}

// Where a warp goes: the picture's swapchain image (acquired), or the curved
// picture's own image. 1 when there is somewhere to draw.
static int videoTarget(XrCtx* ctx, Video* vd, XrCurve* curve, int curved, uint32_t* idx, int* swapAcquired) {
    *swapAcquired = 0;
    if (curved) {
        // The curve's own image is the picture's size, one per eye with a
        // stereo chain (native, lane eye-swapchains; it was as wide as the
        // chain), the supersampled picture's one (never in 3D)
        int pics = curve->mode == 1 && vd->width != vd->picW ? 2 : 1;
        return curvePicture(ctx, curve, vd->picW, vd->height, pics) == 0;
    }
    XrSwapchainImageAcquireInfo ai = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    XrSwapchainImageWaitInfo wi = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    wi.timeout = XR_INFINITE_DURATION;
    if (!checkXr(xrAcquireSwapchainImage(vd->swapchain, &ai, idx), "acquire video image")) {
        return 0;
    }
    if (!checkXr(xrWaitSwapchainImage(vd->swapchain, &wi), "wait video image")) {
        XrSwapchainImageReleaseInfo rl = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
        xrReleaseSwapchainImage(vd->swapchain, &rl);
        return 0;
    }
    *swapAcquired = 1;
    return 1;
}

// ---- the supersampled picture (M5) ------------------------------------------------
// Upstream asked the compositor to supersample the picture's layer (FB layer
// settings, NORMAL or QUALITY); SteamVR has none. Natively, with the Display
// tab's Supersample row on, the warp goes into an image of the app's own and
// mlxr_pass_supersample draws it, filtered over each display pixel's
// footprint, into a swapchain the size the picture covers on the display; the
// quad layer shows that one. Only while the picture is minified by 1.3 or
// more (tests/m5 supersample: at the default placement, 2.79, the error
// against the ideal halves when the compositor samples bilinear from a one
// level image). Sizes snap to 16 px and change by 15% or more, never in a drag.

#define SUPER_MIN_RATIO 1.3f
#define SUPER_IMAGES 8

typedef struct XrSuper {
    XrCurve pic;                // the warp's own image (the curve's kind)
    XrSwapchain swapchain;
    uint32_t count;
    XrSwapchainImageVulkan2KHR images[SUPER_IMAGES];
    VkImageView views[SUPER_IMAGES];
    int w, h;                   // the swapchain's
    int wantW, wantH;           // this frame's
    float ratio;
    int failed;
    int acquired;
    int filled;
    int saidW;
} XrSuper;

static void superDestroySwapchain(XrCtx* ctx, XrSuper* ss) {
    for (uint32_t i = 0; i < ss->count; i++) vkDestroyImageView(ctx->vk->device, ss->views[i], NULL);
    ss->count = 0;
    if (ss->swapchain) {
        xrDestroySwapchain(ss->swapchain);
        ctx->swapchainsAlive--;
    }
    ss->swapchain = XR_NULL_HANDLE;
    ss->w = ss->h = 0;
    ss->filled = 0;
}

static void superDestroy(XrCtx* ctx, XrSuper* ss) {
    superDestroySwapchain(ctx, ss);
    curveFreePicture(ctx->vk, &ss->pic);
}

// Whether the picture is supersampled this frame, and at what size
static int superWanted(XrCtx* ctx, const Video* vd, XrSuper* ss, int curvedNow) {
    // M6: not while the 3D is on (each eye has its own picture)
    if (ctx->supersampleMode <= 0 || curvedNow || ss->failed || ctx->launcher != LAUNCHER_NONE || !vd->swapchain
            || (ctx->stereoMode != DEPTH_MODE_OFF && ctx->stereoLive) || ctx->recommendedEyeWidth <= 0) {
        return 0;
    }
    XrPosef pose;
    float pw, ph;
    XrSpace space;
    xrPictureView(ctx, &pose, &pw, &ph, &space);
    XrViewLocateInfo li = { XR_TYPE_VIEW_LOCATE_INFO };
    li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    li.displayTime = ctx->predictedDisplayTime;
    li.space = space;
    XrViewState vs = { XR_TYPE_VIEW_STATE };
    XrView views[2] = { { XR_TYPE_VIEW }, { XR_TYPE_VIEW } };
    uint32_t n = 0;
    if (XR_FAILED(xrLocateViews(ctx->session, &li, &vs, 2, &n, views)) || n < 2) {
        return ss->filled && ss->w > 0;
    }
    // How many of the eye's pixels the picture covers across, from the
    // eye's middle: its angle over the eye's pixels a radian
    float ex = pose.position.x - views[0].pose.position.x, ey = pose.position.y - views[0].pose.position.y,
          ez = pose.position.z - views[0].pose.position.z;
    float d = sqrtf(ex * ex + ey * ey + ez * ez);
    float fov = views[0].fov.angleRight - views[0].fov.angleLeft;
    if (d < 0.05f || fov <= 0.0f) {
        return 0;
    }
    float covered = 2.0f * atanf(0.5f * pw / d) * (float)ctx->recommendedEyeWidth / fov;
    float ratio = (float)vd->picW / covered;
    ss->ratio = ratio;
    if (ratio < SUPER_MIN_RATIO) {
        return 0;
    }
    int tw = ((int)ceilf((float)vd->picW / ratio) + 15) & ~15;
    if (tw < 64) tw = 64;
    int th = (int)lroundf((float)tw * (float)vd->height / (float)vd->picW);
    int dragging = ctx->grabMode != GRAB_NONE || ctx->cogDragSlider >= 0;
    if (ss->swapchain && (dragging || abs(tw - ss->w) * 100 < 15 * ss->w)) {
        tw = ss->w;
        th = ss->h;
    }
    ss->wantW = tw;
    ss->wantH = th;
    return 1;
}

static int superSwapchain(XrCtx* ctx, XrSuper* ss, int64_t format) {
    if (ss->swapchain && ss->w == ss->wantW && ss->h == ss->wantH) {
        return 0;
    }
    superDestroySwapchain(ctx, ss);
    XrSwapchainCreateInfo ci = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
    ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    ci.format = format;
    ci.sampleCount = 1;
    ci.width = (uint32_t)ss->wantW;
    ci.height = (uint32_t)ss->wantH;
    ci.faceCount = ci.arraySize = ci.mipCount = 1;
    if (!checkXr(xrCreateSwapchain(ctx->session, &ci, &ss->swapchain), "create supersampled picture swapchain")) {
        ss->failed = 1;
        return -1;
    }
    ctx->swapchainsAlive++;
    xrEnumerateSwapchainImages(ss->swapchain, 0, &ss->count, NULL);
    if (ss->count > SUPER_IMAGES) ss->count = SUPER_IMAGES;
    for (uint32_t i = 0; i < ss->count; i++) ss->images[i] = (XrSwapchainImageVulkan2KHR){ XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR };
    xrEnumerateSwapchainImages(ss->swapchain, ss->count, &ss->count, (XrSwapchainImageBaseHeader*)ss->images);
    for (uint32_t i = 0; i < ss->count; i++) {
        VkImageViewCreateInfo vi = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        vi.image = ss->images[i].image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = (VkFormat)format;
        vi.subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        if (vkCreateImageView(ctx->vk->device, &vi, NULL, &ss->views[i]) != VK_SUCCESS) return -1;
    }
    ss->w = ss->wantW;
    ss->h = ss->wantH;
    LOGEV("supersampled picture: %dx%d on the display for a %dx%d picture (minification %.2f); swapchains alive %d "
          "of 16", ss->w, ss->h, ctx->videoWidth, ctx->videoHeight, ss->ratio, ctx->swapchainsAlive);
    return 0;
}

// The picture just warped into ss->pic, filtered down into the swapchain
static void superRecord(XrCtx* ctx, XrSuper* ss, MlxrPasses* passes, VkCommandBuffer cmd, int64_t format) {
    if (superSwapchain(ctx, ss, format) != 0) {
        return;
    }
    uint32_t idx = 0;
    if (!xrArtAcquire(ss->swapchain, "acquire supersampled picture image", &idx)) {
        return;
    }
    mlxr_barrier(cmd, ss->images[idx].image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    MlxrTarget t;
    memset(&t, 0, sizeof(t));
    t.view = ss->views[idx];
    t.format = (VkFormat)format;
    t.rect.extent = (VkExtent2D){ (uint32_t)ss->w, (uint32_t)ss->h };
    MlxrSupersampleParams sp;
    memset(&sp, 0, sizeof(sp));
    sp.src = ss->pic.pic[0].view;
    sp.srcRect[2] = sp.srcRect[3] = 1.0f;
    // Normal and Quality, upstream's two levels
    sp.taps = ctx->supersampleMode >= 2 ? 4 : 2;
    mlxr_pass_supersample(passes, cmd, &t, &sp);
    ss->acquired = 1;
    if (ss->saidW != ss->w) {
        ss->saidW = ss->w;
        LOGI("picture supersampled to %dx%d, %d taps", ss->w, ss->h, sp.taps);
    }
}

// ---- small swapchains made by the host ------------------------------------------

typedef struct Frame {
    VkCommandPool pool;
    VkCommandBuffer cmd;
    VkFence fence;
    int recording;
} Frame;

static VkCommandBuffer frameBegin(XrCtx* ctx, Frame* fr) {
    if (fr->recording) {
        return fr->cmd;
    }
    // The buffer may still be on the GPU from the frame's earlier submission
    vkWaitForFences(ctx->vk->device, 1, &fr->fence, VK_TRUE, UINT64_MAX);
    vkResetCommandBuffer(fr->cmd, 0);
    VkCommandBufferBeginInfo cbi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    cbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(fr->cmd, &cbi);
    fr->recording = 1;
    return fr->cmd;
}

static Frame* g_frame; // for xrEnsurePassthroughSwapchain

// One image of the backdrop swapchain cleared to a colour (linear red, green,
// blue, alpha) and left ready to be released
static void clearBackdropImage(VkCommandBuffer cmd, VkImage image, const float linear[4]) {
    mlxr_barrier(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkClearColorValue value = { { linear[0], linear[1], linear[2], linear[3] } };
    VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &value, 1, &range);
    mlxr_barrier(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
}

// The fork's projection layer behind everything: transparent when passthrough
// is on, and since lane wave3-visual also dimmed (black at the Background
// dim's alpha) or, in the void, the void's colour. Made once; a change of
// colour clears the next image the layer will show.
static int ensurePassthroughSwapchain(XrCtx* ctx, int size) {
    // The colour as the _SRGB image holds it: the red, green and blue as typed
    // are cleared as their linear values
    float want[4] = { srgbToLinear(ctx->backdropWant[0]), srgbToLinear(ctx->backdropWant[1]),
                      srgbToLinear(ctx->backdropWant[2]), ctx->backdropWant[3] };
    if (ctx->passthroughSwapchain != XR_NULL_HANDLE) {
        if (ctx->backdropDrawnValid
                && memcmp(ctx->backdropDrawn, ctx->backdropWant, sizeof(ctx->backdropDrawn)) == 0) {
            return 1;
        }
        uint32_t idx = 0;
        XrSwapchainImageAcquireInfo aq = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
        if (!checkXr(xrAcquireSwapchainImage(ctx->passthroughSwapchain, &aq, &idx), "acquire passthrough image")) {
            return 0;
        }
        XrSwapchainImageWaitInfo wi = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
        wi.timeout = XR_INFINITE_DURATION;
        xrWaitSwapchainImage(ctx->passthroughSwapchain, &wi);
        if (idx < ctx->passthroughImageCount) {
            clearBackdropImage(frameBegin(ctx, g_frame), ctx->passthroughImages[idx], want);
        }
        XrSwapchainImageReleaseInfo rl = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
        xrReleaseSwapchainImage(ctx->passthroughSwapchain, &rl);
        memcpy(ctx->backdropDrawn, ctx->backdropWant, sizeof(ctx->backdropDrawn));
        ctx->backdropDrawnValid = 1;
        return 1;
    }
    XrSwapchainCreateInfo ci = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
    ci.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    ci.format = VK_FORMAT_R8G8B8A8_SRGB;
    ci.sampleCount = 1;
    ci.width = ci.height = (uint32_t)size;
    ci.faceCount = ci.arraySize = ci.mipCount = 1;
    XrSwapchain sc = XR_NULL_HANDLE;
    if (!checkXr(xrCreateSwapchain(ctx->session, &ci, &sc), "create passthrough swapchain")) {
        return 0;
    }
    ctx->swapchainsAlive++;
    uint32_t count = 0;
    XrSwapchainImageVulkan2KHR imgs[8];
    xrEnumerateSwapchainImages(sc, 0, &count, NULL);
    if (count > 8) count = 8;
    for (uint32_t i = 0; i < count; i++) imgs[i] = (XrSwapchainImageVulkan2KHR){ XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR };
    xrEnumerateSwapchainImages(sc, count, &count, (XrSwapchainImageBaseHeader*)imgs);
    // Every image cleared to the colour, one acquire each, recorded into the
    // frame's command buffer (submitted before the end of this frame)
    VkCommandBuffer cmd = frameBegin(ctx, g_frame);
    ctx->passthroughImageCount = count;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t idx = 0;
        XrSwapchainImageAcquireInfo aq = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
        if (!checkXr(xrAcquireSwapchainImage(sc, &aq, &idx), "acquire passthrough image")) break;
        XrSwapchainImageWaitInfo wi = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
        wi.timeout = XR_INFINITE_DURATION;
        xrWaitSwapchainImage(sc, &wi);
        if (idx < 8) ctx->passthroughImages[idx] = imgs[idx].image;
        clearBackdropImage(cmd, imgs[idx].image, want);
        XrSwapchainImageReleaseInfo rl = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
        xrReleaseSwapchainImage(sc, &rl);
    }
    memcpy(ctx->backdropDrawn, ctx->backdropWant, sizeof(ctx->backdropDrawn));
    ctx->backdropDrawnValid = 1;
    ctx->passthroughSwapchain = sc;
    LOGEV("passthrough projection layer ready (%dx%d transparent, %u images); swapchains alive %d of 16", size,
          size, count, ctx->swapchainsAlive);
    return 1;
}

// ---- events (upstream pollEvents, handleSessionStateChange) ------------------

static void runtimeEnded(XrCtx* ctx, const char* what) {
    ctx->exitRequested = 1;
    if (ctx->exitReason[0] == '\0') {
        snprintf(ctx->exitReason, sizeof(ctx->exitReason), "%s", what);
        LOGEV("frame loop ending: %s", ctx->exitReason);
    }
}

static void handleSessionStateChange(XrCtx* ctx, XrSessionState newState, const XrAppHooks* hk) {
    LOGEV("session state %d -> %d", ctx->sessionState, newState);
    ctx->sessionState = newState;
    if (hk->focus) hk->focus(hk->user, newState == XR_SESSION_STATE_FOCUSED);
    switch (newState) {
        case XR_SESSION_STATE_READY: {
            XrSessionBeginInfo beginInfo = { XR_TYPE_SESSION_BEGIN_INFO };
            beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            if (checkXr(xrBeginSession(ctx->session, &beginInfo), "xrBeginSession")) {
                ctx->sessionRunning = 1;
                ctx->sessionBegins++;
                LOGEV("session begun (%d begun, %d ended)", ctx->sessionBegins, ctx->sessionEnds);
                displaySessionBegun(ctx);
            }
            break;
        }
        case XR_SESSION_STATE_FOCUSED:
            if (!ctx->everFocused) {
                LOGEV("session focused for the first time");
            }
            ctx->everFocused = 1;
            displayFocused(ctx);
            break;
        case XR_SESSION_STATE_STOPPING:
            checkXr(xrEndSession(ctx->session), "xrEndSession");
            ctx->sessionRunning = 0;
            ctx->sessionEnds++;
            LOGEV("session ended for the runtime (%d begun, %d ended)", ctx->sessionBegins, ctx->sessionEnds);
            break;
        case XR_SESSION_STATE_EXITING:
        case XR_SESSION_STATE_LOSS_PENDING:
            ctx->sessionRunning = 0;
            runtimeEnded(ctx, newState == XR_SESSION_STATE_EXITING ? "session exiting" : "session loss pending");
            break;
        default:
            break;
    }
}

static void pollEvents(XrCtx* ctx, const XrAppHooks* hk) {
    XrEventDataBuffer event;
    for (;;) {
        event.type = XR_TYPE_EVENT_DATA_BUFFER;
        event.next = NULL;
        if (xrPollEvent(ctx->instance, &event) != XR_SUCCESS) {
            break;
        }
        switch (event.type) {
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
                handleSessionStateChange(ctx, ((XrEventDataSessionStateChanged*)&event)->state, hk);
                break;
            case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING: {
                XrEventDataReferenceSpaceChangePending* change = (XrEventDataReferenceSpaceChangePending*)&event;
                if (change->referenceSpaceType == XR_REFERENCE_SPACE_TYPE_LOCAL) {
                    recentreScreen(ctx);
                    // The head jumps in the space when the change lands, which
                    // head aim must not read as a turn
                    XrTime hold = ctx->predictedDisplayTime + HEAD_AIM_RECENTRE_HOLD_NS;
                    ctx->headAimRecentring = 1;
                    ctx->headAimRecentreAt = change->changeTime > hold ? change->changeTime : hold;
                }
                break;
            }
            case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
                refreshInputSource(ctx);
                break;
            case XR_TYPE_EVENT_DATA_DISPLAY_REFRESH_RATE_CHANGED_FB: {
                XrEventDataDisplayRefreshRateChangedFB* rate = (XrEventDataDisplayRefreshRateChangedFB*)&event;
                displayRateChanged(ctx, rate->fromDisplayRefreshRate, rate->toDisplayRefreshRate);
                break;
            }
            case XR_TYPE_EVENT_DATA_USER_PRESENCE_CHANGED_EXT: {
                // Native (lane wave3-session): the headset off or on
                const XrEventDataUserPresenceChangedEXT* up = (const XrEventDataUserPresenceChangedEXT*)&event;
                LOGEV("user presence: headset %s", up->isUserPresent ? "on" : "off");
                if (hk->presence) hk->presence(hk->user, up->isUserPresent ? 1 : 0);
                break;
            }
            case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
                runtimeEnded(ctx, "instance loss pending");
                break;
            default:
                break;
        }
    }
}

// ---- the instance (upstream initXrInstance, Vulkan in place of GL ES) ----------

// Native (lane av-latency): XR_KHR_convert_timespec_time puts the runtime's
// predicted display times on CLOCK_MONOTONIC, for the log's latency line
// (how far ahead poses are predicted, decoder submit to photons)
typedef XrResult(XRAPI_PTR* ConvertTimeFn)(XrInstance instance, XrTime time, struct timespec* out);
static int g_convertTimeOffered;
static ConvertTimeFn g_xrTimeToTimespec;
// Native (lane wave3-session): XR_EXT_user_presence is offered and enabled (the headset's on and off
// come as events), and the system says it has the sensor
static int g_presenceOffered, g_presenceSupported;

// 0 when the runtime cannot say
static int64_t xrTimeToMonoNs(XrInstance instance, XrTime t) {
    struct timespec ts;
    if (!g_xrTimeToTimespec || XR_FAILED(g_xrTimeToTimespec(instance, t, &ts))) return 0;
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static int offered(const XrExtensionProperties* exts, uint32_t n, const char* name) {
    for (uint32_t i = 0; i < n; i++) {
        if (!strcmp(exts[i].extensionName, name)) return 1;
    }
    return 0;
}

static int initInstance(XrCtx* ctx, const Settings* s) {
    uint32_t n = 0;
    if (XR_FAILED(xrEnumerateInstanceExtensionProperties(NULL, 0, &n, NULL))) {
        LOGE("no OpenXR runtime reachable: is SteamVR running?");
        return 0;
    }
    XrExtensionProperties* exts = calloc(n, sizeof(*exts));
    for (uint32_t i = 0; i < n; i++) exts[i].type = XR_TYPE_EXTENSION_PROPERTIES;
    xrEnumerateInstanceExtensionProperties(NULL, n, &n, exts);
    if (!offered(exts, n, XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME)) {
        LOGE("runtime lacks XR_KHR_vulkan_enable2");
        free(exts);
        return 0;
    }
    ctx->cylinderSupported = 0; // none on SteamVR; the curve is the host's (see curve below)
    ctx->frameInteraction = offered(exts, n, "XR_VALVE_frame_controller_interaction");
    ctx->refreshRateSupported = offered(exts, n, XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
    ctx->perfSettingsSupported = offered(exts, n, XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME);
    ctx->metricsSupported = offered(exts, n, XR_META_PERFORMANCE_METRICS_EXTENSION_NAME);
    ctx->handInteraction = offered(exts, n, XR_EXT_HAND_INTERACTION_EXTENSION_NAME);
    ctx->msftHandInteraction = offered(exts, n, XR_MSFT_HAND_INTERACTION_EXTENSION_NAME);
    ctx->handTracking = offered(exts, n, XR_EXT_HAND_TRACKING_EXTENSION_NAME);
    ctx->handAimOffered = offered(exts, n, XR_FB_HAND_TRACKING_AIM_EXTENSION_NAME);
    ctx->eyeGaze = offered(exts, n, XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME);
    g_convertTimeOffered = offered(exts, n, "XR_KHR_convert_timespec_time");
    g_presenceOffered = offered(exts, n, XR_EXT_USER_PRESENCE_EXTENSION_NAME);
    g_presenceSupported = 0;
    ctx->handsEnabled = s->hand_tracking;
    if (!ctx->handsEnabled) {
        ctx->handInteraction = 0;
        ctx->msftHandInteraction = 0;
        ctx->handTracking = 0;
        ctx->handAimOffered = 0;
        LOGI("hand tracking off by preference");
    }
    // Native: the gaze extension only when the setting wants it, so a runtime
    // that would ask for eye tracking does not ask for nothing
    if (!s->gaze) {
        ctx->eyeGaze = 0;
    }
    free(exts);

    const char* enabled[16];
    uint32_t ne = 0;
    enabled[ne++] = XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME;
    if (ctx->frameInteraction) enabled[ne++] = "XR_VALVE_frame_controller_interaction";
    if (ctx->refreshRateSupported) enabled[ne++] = XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME;
    if (ctx->perfSettingsSupported) enabled[ne++] = XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME;
    if (ctx->metricsSupported) enabled[ne++] = XR_META_PERFORMANCE_METRICS_EXTENSION_NAME;
    if (ctx->handInteraction) enabled[ne++] = XR_EXT_HAND_INTERACTION_EXTENSION_NAME;
    if (ctx->handTracking) enabled[ne++] = XR_EXT_HAND_TRACKING_EXTENSION_NAME;
    ctx->handAimOffered = ctx->handAimOffered && ctx->handTracking;
    if (ctx->handAimOffered) enabled[ne++] = XR_FB_HAND_TRACKING_AIM_EXTENSION_NAME;
    if (ctx->eyeGaze) enabled[ne++] = XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME;
    if (ctx->msftHandInteraction) enabled[ne++] = XR_MSFT_HAND_INTERACTION_EXTENSION_NAME;
    if (g_convertTimeOffered) enabled[ne++] = "XR_KHR_convert_timespec_time";
    if (g_presenceOffered) enabled[ne++] = XR_EXT_USER_PRESENCE_EXTENSION_NAME;

    XrInstanceCreateInfo ici = { XR_TYPE_INSTANCE_CREATE_INFO };
    strcpy(ici.applicationInfo.applicationName, "Moonlight XR for Frame");
    strcpy(ici.applicationInfo.engineName, "mlxr");
    ici.applicationInfo.applicationVersion = 4;
    ici.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    ici.enabledExtensionCount = ne;
    ici.enabledExtensionNames = enabled;
    if (!checkXr(xrCreateInstance(&ici, &ctx->instance), "xrCreateInstance")) {
        return 0;
    }
    probeDisplayExtensions(ctx);
    xrMetricsProbe(ctx);
    g_xrTimeToTimespec = NULL;
    if (g_convertTimeOffered
        && XR_FAILED(xrGetInstanceProcAddr(ctx->instance, "xrConvertTimeToTimespecTimeKHR",
                                           (PFN_xrVoidFunction*)&g_xrTimeToTimespec))) {
        g_xrTimeToTimespec = NULL;
    }
    XrInstanceProperties ip = { XR_TYPE_INSTANCE_PROPERTIES };
    if (XR_SUCCEEDED(xrGetInstanceProperties(ctx->instance, &ip))) {
        snprintf(ctx->runtimeLabel, sizeof(ctx->runtimeLabel), "%s %u.%u.%u", ip.runtimeName,
                 (unsigned)XR_VERSION_MAJOR(ip.runtimeVersion), (unsigned)XR_VERSION_MINOR(ip.runtimeVersion),
                 (unsigned)XR_VERSION_PATCH(ip.runtimeVersion));
        LOGEV("runtime %s; frame controller %d, refresh rate %d, hands %d, gaze %d", ctx->runtimeLabel,
              ctx->frameInteraction, ctx->refreshRateSupported, ctx->handTracking, ctx->eyeGaze);
    }
    XrSystemGetInfo sgi = { XR_TYPE_SYSTEM_GET_INFO };
    sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (!checkXr(xrGetSystem(ctx->instance, &sgi, &ctx->systemId), "xrGetSystem (no headset?)")) {
        return 0;
    }
    uint32_t nm = 0;
    XrEnvironmentBlendMode modes[8];
    xrEnumerateEnvironmentBlendModes(ctx->instance, ctx->systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 8,
                                     &nm, modes);
    for (uint32_t i = 0; i < nm; i++) {
        if (modes[i] == XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND) ctx->alphaBlendSupported = 1;
    }
    LOGEV("passthrough %s", ctx->alphaBlendSupported ? "available (alpha blend)" : "not offered by this runtime");
    ctx->maxLayerCount = XR_MIN_COMPOSITION_LAYERS_SUPPORTED;
    XrSystemEyeGazeInteractionPropertiesEXT gazeProps = { XR_TYPE_SYSTEM_EYE_GAZE_INTERACTION_PROPERTIES_EXT };
    XrSystemHandTrackingPropertiesEXT handProps = { XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT };
    XrSystemUserPresencePropertiesEXT presenceProps = { XR_TYPE_SYSTEM_USER_PRESENCE_PROPERTIES_EXT };
    XrSystemProperties props = { XR_TYPE_SYSTEM_PROPERTIES };
    if (g_presenceOffered) {
        presenceProps.next = props.next;
        props.next = &presenceProps;
    }
    if (ctx->eyeGaze) {
        gazeProps.next = props.next;
        props.next = &gazeProps;
    }
    if (ctx->handTracking) {
        handProps.next = props.next;
        props.next = &handProps;
    }
    if (XR_SUCCEEDED(xrGetSystemProperties(ctx->instance, ctx->systemId, &props))) {
        if (props.graphicsProperties.maxLayerCount > 0
                && (int)props.graphicsProperties.maxLayerCount < ctx->maxLayerCount) {
            ctx->maxLayerCount = (int)props.graphicsProperties.maxLayerCount;
        }
        LOGEV("system %s, %d composition layers used", props.systemName, ctx->maxLayerCount);
        if (ctx->eyeGaze && !gazeProps.supportsEyeGazeInteraction) ctx->eyeGaze = 0;
        if (ctx->handTracking && !handProps.supportsHandTracking) ctx->handTracking = 0;
        g_presenceSupported = g_presenceOffered && presenceProps.supportsUserPresence;
    }
    if (g_presenceOffered) {
        LOGEV("user presence: XR_EXT_user_presence enabled, the system %s the sensor",
              g_presenceSupported ? "has" : "does not report");
    }
    else {
        LOGI("user presence: XR_EXT_user_presence not offered by this runtime: headset-off handling is off");
    }
    XrViewConfigurationView cv[2] = { { XR_TYPE_VIEW_CONFIGURATION_VIEW }, { XR_TYPE_VIEW_CONFIGURATION_VIEW } };
    uint32_t ncv = 0;
    if (XR_SUCCEEDED(xrEnumerateViewConfigurationViews(ctx->instance, ctx->systemId,
                                                       XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &ncv, cv))
            && ncv > 0) {
        ctx->recommendedEyeWidth = (int)cv[0].recommendedImageRectWidth;
        ctx->recommendedEyeHeight = (int)cv[0].recommendedImageRectHeight;
        ctx->maxEyeWidth = (int)cv[0].maxImageRectWidth;
        ctx->maxEyeHeight = (int)cv[0].maxImageRectHeight;
    }
    return 1;
}

// ---- the host's per frame glue (XrRenderer.dispatchInput and friends) -----------

typedef struct Host {
    XrCtx* ctx;
    const XrAppConfig* cfg;
    Settings live;             // config.ini as the panels should show it now
    float out[IN_SLOTS];
    int heldButtons;
    int heldKbMods;
    int lastX, lastY;
    int pointerSent;           // Native (av-latency): this frame sent the host a mouse move
    float distance, quadWidth, curvature; // the preference's placement
    char statsText[1536]; // Native: was 512, which cut the last lines off once the app, perf and depth lines grew
    int64_t statsNs;
    char clockText[32];
    // Native (lane wave3-hud): the headset battery (read every 30 s), the stream's figures a second at a time
    // (the HUD, the slow connection badge), the compositor's timing, and the perf CSV
    int64_t batteryNs, netNs;
    char batteryDir[600];
    MlxrHudCum hudPrev;
    MlxrHudFigures fig;
    uint64_t hudVideo;
    MlxrSlowConn slow;
    MlxrCompositorMetrics comp;
    int metricsStarted;
    FILE* perfFile;
    int64_t perfNs, perfStartNs;
    MlxrHudCum perfPrev;
    uint64_t perfVideo;
} Host;

static void saveSettings(Host* h, const char* why) {
    Settings* s = h->cfg->settings;
    int r = settings_save(s, h->cfg->settingsPath);
    if (r != 0) {
        LOGW("config.ini not saved (%s): error %d", why, r);
    }
    else {
        LOGI("config.ini saved (%s)", why);
    }
}

// The IN_ slots the input pass filled, to the app, the panels and config.ini
static void dispatchInput(Host* h, XrUiEvents* ev) {
    XrCtx* ctx = h->ctx;
    const XrAppHooks* hk = &h->cfg->hooks;
    float* in = h->out;
    int streaming = atomic_load(&g_streaming);
    if (streaming) {
        if (in[IN_HIT] != 0.0f && hk->pointer) {
            int w = ctx->videoWidth, hgt = ctx->videoHeight;
            // Native (PLAN 4.3): on a host-rendered pair, onto the left eye's half
            float pu = in[IN_U], pv = in[IN_V];
            xrDepthPointerUv(ctx, &pu, &pv);
            int x = (int)(pu * (float)w), y = (int)(pv * (float)hgt);
            x = x < 0 ? 0 : (x > w - 1 ? w - 1 : x);
            y = y < 0 ? 0 : (y > hgt - 1 ? hgt - 1 : y);
            if (x != h->lastX || y != h->lastY) {
                hk->pointer(hk->user, x, y, w, hgt);
                h->lastX = x;
                h->lastY = y;
                h->pointerSent = 1;
            }
        }
        int dx = (int)in[IN_MOUSE_DX], dy = (int)in[IN_MOUSE_DY];
        if ((dx != 0 || dy != 0) && hk->mouseMove) {
            h->lastX = h->lastY = -1;
            hk->mouseMove(hk->user, dx, dy);
            h->pointerSent = 1;
        }
    }
    // A release always goes, so nothing sticks on the host
    int buttons = (int)in[IN_BUTTONS];
    int changed = buttons ^ h->heldButtons;
    // Native (lane wave2-bind-wire): VR_BUTTON_LEFT, RIGHT, MIDDLE and the
    // bindings' X1 and X2 (bits 1 << 0..4) -> BUTTON_* 1, 3, 2, 4, 5
    for (int i = 0; i < XR_BIND_MOUSE_BUTTONS; i++) {
        int mask = 1 << i;
        if ((changed & mask) && hk->button && (streaming || !(buttons & mask))) {
            hk->button(hk->user, xrBindHostButton(i), (buttons & mask) != 0);
        }
    }
    h->heldButtons = buttons;
    // Native (lane wave2-bind-wire): the wheel comes in 120ths of a notch
    int wheel = (int)in[IN_SCROLL];
    if (wheel != 0 && streaming && hk->scroll) {
        hk->scroll(hk->user, wheel);
    }
    int sideways = (int)in[IN_HSCROLL];
    if (sideways != 0 && streaming && hk->hscroll) {
        hk->hscroll(hk->user, sideways);
    }
    // The bindings' keys: a press only while streaming, a release always
    for (int i = 0; i < ctx->bindKeyCount; i++) {
        const XrBindKey* k = &ctx->bindKeys[i];
        if (hk->bindKey && (streaming || !k->down)) {
            hk->bindKey(hk->user, k->vk, k->mods, k->down);
        }
    }
    ctx->bindKeyCount = 0;
    int key = (int)in[IN_KEY];
    if (key >= 0) {
        if (in[IN_REPORT_ZONE] >= 0.0f) {
            xrPanelsReportKey(ctx, key);
        }
        else if (hk->key) {
            hk->key(hk->user, key, (int)in[IN_KEY_MODS]);
        }
    }
    int mods = (int)in[IN_KB_MODS];
    if (mods != h->heldKbMods) {
        h->heldKbMods = mods;
        if (hk->modifiers) hk->modifiers(hk->user, mods);
    }
    if (ctx->pcvrRequest) {
        ctx->pcvrRequest = 0;
        ev->pcvrConfirmed = 1;
    }
    if ((int)in[IN_EXIT] == EXIT_SLOT_DISCONNECT) {
        ev->disconnectConfirmed = 1;
    }
    else if (in[IN_EXIT] != 0.0f) {
        ev->exitConfirmed = 1;
    }
    int report = (int)in[IN_REPORT];
    if (report >= 0) {
        xrPanelsReportEvent(ctx, report, h->cfg->reportDir);
    }
    int toast = (int)in[IN_TOAST];
    if (toast >= 0) {
        xrPanelsToast(ctx, toast, (int)in[IN_TOAST_ARG]);
    }
    Settings* s = h->cfg->settings;
    if (in[IN_POSE_DIRTY] != 0.0f) {
        xrPoseToSettings(&in[IN_POSE], xrCurveSetting(ctx), s);
        saveSettings(h, "screen placement");
    }
    int pick = (int)in[IN_PICKER_PICK];
    if (pick >= 0 && pick < ctx->pickerCells) {
        xrSetEnvironment(ctx, pick);
        ctx->passthrough = pick == ENV_CELL_PASSTHROUGH;
        // environment: 1 passthrough, 2 void, 3..5 the rooms (settings.c ENV_NAMES)
        s->environment = xrEnvironmentSetting(pick);
        s->passthrough = pick == ENV_CELL_PASSTHROUGH;
        saveSettings(h, "environment");
    }
    if (in[IN_HINT] != 0.0f) {
        s->hand_hint_seen = 1;
        saveSettings(h, "hand lock hint");
    }
    int setting = (int)in[IN_SETTING];
    if (setting >= 0) {
        if (xrStoreSetting(ctx, s, setting, (int)in[IN_SETTING_VALUE])) {
            saveSettings(h, "panel");
        }
    }
    // The launcher's list took a press
    if (ctx->launcherPressHit >= 0) {
        const UiHits* hits = xrPanelsLauncherHits(ctx);
        if (hits != NULL && ctx->launcherPressHit < hits->count) {
            const UiHit* hit = &hits->hit[ctx->launcherPressHit];
            ev->listKind = hit->kind;
            ev->listA = hit->a;
            ev->listB = hit->b;
        }
        ctx->launcherPressHit = -1;
    }
    ev->focused = ctx->sessionState == XR_SESSION_STATE_FOCUSED;
    // Native (lane layout): Follow, the angular-size lock and the layout kept
    // per PC and app, after everything the input pass changed and before the
    // layers are built
    if (xrLayoutFrame(ctx, s, nowNs())) {
        saveSettings(h, "app layout");
    }
    // M6 (native): the 3D switch is remembered, unless the model failing
    // turned it off
    if (ctx->stereoMode != DEPTH_MODE_OFF && !ctx->depthFailed && s->stereo != ctx->stereoLive) {
        s->stereo = ctx->stereoLive;
        saveSettings(h, "3d switch");
    }
}

// Native (lane wave3-hud): the metrics counters are switched on once the session runs
static void metricsEnsure(Host* h) {
    if (!h->metricsStarted && h->ctx->sessionRunning && h->ctx->metricsSupported) {
        h->metricsStarted = 1;
        xrMetricsStart(h->ctx);
    }
}

// Native (lane wave3-hud): the headset battery for the clock strip, every 30 s. A machine without
// one (the desktop) shows nothing.
static void updateBattery(Host* h, int64_t now) {
    XrCtx* ctx = h->ctx;
    if (h->batteryNs != 0 && now - h->batteryNs < 30000000000LL) {
        return;
    }
    h->batteryNs = now;
    MlxrBattery b;
    memset(&b, 0, sizeof(b));
    if (h->batteryDir[0] == '\0') {
        mlxr_battery_find(NULL, h->batteryDir, sizeof(h->batteryDir));
    }
    if (h->batteryDir[0] != '\0' && !mlxr_battery_read(h->batteryDir, &b)) {
        h->batteryDir[0] = '\0';   // gone: look again next time
    }
    if (b.present && !ctx->batteryPresent) {
        LOGI("battery: %d%% (%s)", b.percent, b.charging ? "charging" : "not charging");
    }
    ctx->batteryPresent = b.present;
    ctx->batteryPercent = b.percent;
    ctx->batteryCharging = b.charging;
}

// perf-<stamp>.csv next to vrtiming.log: a row every 5 s while the stats are on and a stream is up
static void perfClose(Host* h) {
    if (h->perfFile != NULL) {
        fclose(h->perfFile);
        h->perfFile = NULL;
    }
}

static void perfRow(Host* h, int64_t now, uint64_t videoFrames) {
    XrCtx* ctx = h->ctx;
    MlxrHudCum cum;
    memset(&cum, 0, sizeof(cum));
    if (!ctx->overlayVisible || !atomic_load(&g_streaming) || h->cfg->hooks.hud == NULL) {
        perfClose(h);
        h->perfNs = 0;
        return;
    }
    if (h->perfNs != 0 && now - h->perfNs < 5000000000LL) {
        return;
    }
    if (!h->cfg->hooks.hud(h->cfg->hooks.user, &cum)) {
        perfClose(h);
        h->perfNs = 0;
        return;
    }
    if (h->perfNs == 0) {
        h->perfNs = now;
        h->perfPrev = cum;
        h->perfVideo = videoFrames;
        h->perfStartNs = now;
        return;
    }
    double secs = (double)(now - h->perfNs) / 1e9;
    MlxrHudFigures fig;
    mlxr_hud_window(&h->perfPrev, &cum, secs, (double)(videoFrames - h->perfVideo) / secs, &fig);
    h->perfPrev = cum;
    h->perfNs = now;
    h->perfVideo = videoFrames;
    if (h->perfFile == NULL) {
        const char* dir = getenv("MLXR_LOG_DIR");
        const char* home = getenv("HOME");
        char base[400], path[480];
        if (dir != NULL && dir[0] != '\0') {
            snprintf(base, sizeof(base), "%s", dir);
        }
        else {
            snprintf(base, sizeof(base), "%s/Games/MoonlightXRNative", home != NULL ? home : ".");
        }
        time_t t = time(NULL);
        struct tm tm;
        localtime_r(&t, &tm);
        if (!mlxr_perf_path(base, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
                            path, sizeof(path))) {
            return;
        }
        h->perfFile = fopen(path, "w");
        if (h->perfFile == NULL) {
            LOGW("perf csv: %s cannot be written", path);
            return;
        }
        char head[512];
        size_t hn = mlxr_perf_header(head, sizeof(head));
        if (hn > 0) fwrite(head, 1, hn, h->perfFile);
        h->perfStartNs = now - (int64_t)(secs * 1e9);
        LOGEV("perf csv: a row every 5 s into %s", path);
    }
    const Settings* s = h->cfg->settings;
    MlxrPerfRow r;
    memset(&r, 0, sizeof(r));
    r.tSec = (double)(now - h->perfStartNs) / 1e9;
    r.width = ctx->videoWidth;
    r.height = ctx->videoHeight;
    r.streamFps = h->cfg->fps;
    // Native (lane wave4b-abr): the bitrate the stream started with (a path's own setting may have replaced the launcher's)
    r.bitrateKbps = fig.setKbps > 0 ? fig.setKbps
                    : settings_bitrate_kbps(s) > 0 ? settings_bitrate_kbps(s)
                                                   : settings_default_bitrate_kbps(r.width, r.height, r.streamFps);
    r.codec = s->codec;
    r.displayHz = xrGetDisplayRate(ctx);
    r.stereo = ctx->stereoMode != DEPTH_MODE_OFF && ctx->stereoLive;
    r.environment = s->environment;
    r.sharpen = ctx->sharpenMode;
    r.supersample = ctx->supersampleMode;
    r.slow = ctx->slowConn;
    r.fig = fig;
    metricsEnsure(h);
    xrMetricsSample(ctx, &r.comp);
    char row[512];
    size_t n = mlxr_perf_row(&r, row, sizeof(row));
    if (n > 0) {
        fwrite(row, 1, n, h->perfFile);
        fflush(h->perfFile);
    }
}

static void updateTexts(Host* h, int64_t now, uint64_t frames, uint64_t videoFrames) {
    XrCtx* ctx = h->ctx;
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    snprintf(h->clockText, sizeof(h->clockText), "%02d:%02d", tm.tm_hour, tm.tm_min);
    updateBattery(h, now);
    // The stream's figures a second at a time: the HUD, and the slow connection badge, which is up
    // whether or not the stats are
    if (h->netNs == 0 || now - h->netNs >= 1000000000LL) {
        double netSecs = h->netNs > 0 ? (double)(now - h->netNs) / 1e9 : 1.0;
        MlxrHudCum cum;
        memset(&cum, 0, sizeof(cum));
        int have = atomic_load(&g_streaming) && h->cfg->hooks.hud && h->cfg->hooks.hud(h->cfg->hooks.user, &cum);
        if (have) {
            mlxr_hud_window(&h->hudPrev, &cum, netSecs, (double)(videoFrames - h->hudVideo) / netSecs, &h->fig);
            h->hudPrev = cum;
        }
        else {
            memset(&h->fig, 0, sizeof(h->fig));
            h->hudPrev.valid = 0;
        }
        h->hudVideo = videoFrames;
        h->netNs = now;
        int was = ctx->slowConn;
        ctx->slowConn = mlxr_slow_update(&h->slow, now / 1000000LL, have ? &h->fig : NULL);
        if (ctx->slowConn != was) {
            LOGEV("slow connection badge %s (loss %.1f%%, rtt %.0f ms, status %s)", ctx->slowConn ? "up" : "down",
                  h->fig.lossPct, h->fig.rttMs, h->fig.poor ? "poor" : "okay");
        }
    }
    perfRow(h, now, videoFrames);
    if (!ctx->overlayVisible || now - h->statsNs < 1000000000LL) {
        return;
    }
    static uint64_t lastFrames, lastVideo;
    double secs = h->statsNs > 0 ? (now - h->statsNs) / 1e9 : 1.0;
    h->statsNs = now;
    metricsEnsure(h);
    xrMetricsSample(ctx, &h->comp);
    // Native (lane wave3-hud): the HUD and Lite styles, while a stream gives figures
    if (ctx->statsStyle != STATS_STYLE_FULL && h->fig.valid) {
        if (ctx->statsStyle == STATS_STYLE_LITE) {
            mlxr_hud_lite(&h->fig, ctx->slowConn, h->statsText, sizeof(h->statsText));
        }
        else {
            mlxr_hud_panel(&h->fig, ctx->slowConn, &h->comp, xrGetDisplayRate(ctx), h->statsText,
                           sizeof(h->statsText));
        }
        lastFrames = frames;
        lastVideo = videoFrames;
        return;
    }
    char app[768] = "";
    if (h->cfg->hooks.stats) h->cfg->hooks.stats(h->cfg->hooks.user, app, sizeof(app));
    int n = snprintf(h->statsText, sizeof(h->statsText), "Display %.0f Hz, %.0f fps\nVideo %dx%d, %.0f fps\n",
                     xrGetDisplayRate(ctx), (frames - lastFrames) / secs, ctx->videoWidth, ctx->videoHeight,
                     (videoFrames - lastVideo) / secs);
    lastFrames = frames;
    lastVideo = videoFrames;
    // The app's line, one clause a line
    for (const char* p = app; *p && n < (int)sizeof(h->statsText) - 2; p++) {
        if (p[0] == ';' && p[1] == ' ') {
            h->statsText[n++] = '\n';
            p++;
        }
        else {
            h->statsText[n++] = *p;
        }
    }
    h->statsText[n] = '\0';
    // M6: the depth model's lines (upstream's overlay: rate and target,
    // inference, age, skips)
    char depth[320];
    xrDepthOverlay(ctx, depth, sizeof(depth));
    if (depth[0] && n < (int)sizeof(h->statsText) - 2) {
        n += snprintf(h->statsText + n, sizeof(h->statsText) - (size_t)n, "\n%s", depth);
    }
    // Native (lane wave3-hud): what the compositor reports (XR_META_performance_metrics)
    char comp[96];
    mlxr_compositor_line(&h->comp, comp, sizeof(comp));
    if (comp[0] && n < (int)sizeof(h->statsText) - 2) {
        snprintf(h->statsText + n, sizeof(h->statsText) - (size_t)n, "\n%s", comp);
    }
}

// ---- the click (upstream XrClickSound) -------------------------------------------
// Started off the frame loop the first time the setting is on (it talks to
// PulseAudio as it opens); audio/click.c plays it without blocking.

static atomic_int g_clickState; // 0 not started, 1 starting, 2 ready, 3 failed
static pthread_t g_clickThread;

static void* clickOpen(void* arg) {
    (void)arg;
    char err[160] = "";
    if (mlxr_click_init(NULL, err, sizeof(err)) == 0) {
        atomic_store(&g_clickState, 2);
        MLOG_I("click sound ready");
    }
    else {
        atomic_store(&g_clickState, 3);
        MLOG_W("click sound unavailable: %s", err);
    }
    return NULL;
}

static void clickPlay(void) {
    if (atomic_load(&g_clickState) == 2) {
        mlxr_click_play(MLXR_CLICK_PRESS);
    }
}

static void clickStart(void) {
    int expected = 0;
    if (!atomic_compare_exchange_strong(&g_clickState, &expected, 1)) {
        return;
    }
    xrClickPlayer = clickPlay;
    if (pthread_create(&g_clickThread, NULL, clickOpen, NULL) != 0) {
        atomic_store(&g_clickState, 3);
    }
}

static void clickStop(void) {
    int state = atomic_load(&g_clickState);
    if (state == 0) {
        return;
    }
    pthread_join(g_clickThread, NULL);
    if (atomic_load(&g_clickState) == 2) {
        mlxr_click_shutdown();
    }
    atomic_store(&g_clickState, 0);
    xrClickPlayer = NULL;
}

// ---- run -------------------------------------------------------------------------

static void onSignal(int sig) {
    (void)sig;
    atomic_store(&g_quit, 1);
}

int xr_app_run(const XrAppConfig* cfg) {
    int rc = 1;
    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);
    atomic_store(&g_quit, 0);
    atomic_store(&g_streaming, cfg->startStreaming);

    XrCtx* ctx = calloc(1, sizeof(XrCtx));
    Host host;
    memset(&host, 0, sizeof(host));
    host.ctx = ctx;
    host.cfg = cfg;
    host.lastX = host.lastY = -1;
    mlxr_slow_init(&host.slow);
    xrDefaultPlacement(&host.distance, &host.quadWidth, &host.curvature);
    xrCtxDefaults(ctx, cfg->videoWidth, cfg->videoHeight, cfg->fps);

    XrVk vk;
    memset(&vk, 0, sizeof(vk));
    Video vd;
    memset(&vd, 0, sizeof(vd));
    Frame fr;
    memset(&fr, 0, sizeof(fr));
    XrCurve curve;
    memset(&curve, 0, sizeof(curve));
    curve.mode = 1;
    XrSuper ss;
    memset(&ss, 0, sizeof(ss));
    g_frame = &fr;
    MlxrPasses* passes = NULL;

    if (!initInstance(ctx, cfg->settings)) {
        if (ctx->instance) xrDestroyInstance(ctx->instance);
        free(ctx);
        return 1;
    }
    uint32_t nImpExt = 0;
    const char* const* impExt = mlxr_vkimp_device_extensions(&nImpExt);
    if (xr_vk_create(ctx->instance, ctx->systemId, impExt, nImpExt, &vk)) goto fail;
    ctx->vk = &vk;

    XrGraphicsBindingVulkan2KHR gb = { XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR };
    gb.instance = vk.instance;
    gb.physicalDevice = vk.phys;
    gb.device = vk.device;
    gb.queueFamilyIndex = vk.queueFamily;
    XrSessionCreateInfo sci = { XR_TYPE_SESSION_CREATE_INFO };
    sci.next = &gb;
    sci.systemId = ctx->systemId;
    if (!checkXr(xrCreateSession(ctx->instance, &sci, &ctx->session), "xrCreateSession")) goto fail;
    XrReferenceSpaceCreateInfo rsci = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    rsci.poseInReferenceSpace.orientation.w = 1;
    rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    if (!checkXr(xrCreateReferenceSpace(ctx->session, &rsci, &ctx->localSpace), "create local space")) goto fail;
    rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    if (!checkXr(xrCreateReferenceSpace(ctx->session, &rsci, &ctx->viewSpace), "create view space")) goto fail;
    startDisplay(ctx);
    startPerfLevels(ctx);

    ctx->ensurePassthrough = ensurePassthroughSwapchain;
    // Native: the curve is the host's (mlxr_pass_panel into a projection
    // layer), so the Screen tab's Curve row is live
    ctx->cylinderSupported = 1;
    // M6: the depth model's engine (when the depth library is built in) and
    // where its model files are, both before the settings pick the 3D mode
#ifdef MLXR_HAVE_DEPTH
    xrDepthSetEngineOps(xrDepthRealOps());
#endif
    xrWorldFindAssets(ctx);
    // The settings, as nativeInit's arguments and the setters handed them down
    xrApplySettings(ctx, cfg->settings);
    // Native (lane wave2-bind-wire): bindings.ini, written the first time
    xrBindLoad(ctx, NULL);

    if (!initXrInput(ctx)) {
        LOGW("controller input unavailable, pointer off");
        destroyXrInput(ctx);
    }

    // The picture's format: sRGB, as upstream's GL_SRGB8_ALPHA8
    {
        int64_t formats[32];
        uint32_t nf = 0;
        xrEnumerateSwapchainFormats(ctx->session, 32, &nf, formats);
        for (uint32_t i = 0; i < nf && !vd.format; i++)
            if (formats[i] == VK_FORMAT_R8G8B8A8_SRGB || formats[i] == VK_FORMAT_B8G8R8A8_SRGB) vd.format = formats[i];
        if (!vd.format) {
            LOGE("no sRGB swapchain format");
            goto fail;
        }
        ctx->swapchainFormat = vd.format;
    }

    VkCommandPoolCreateInfo pci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = vk.queueFamily;
    if (!VK_OK(vkCreateCommandPool(vk.device, &pci, NULL, &fr.pool), "command pool")) goto fail;
    VkCommandBufferAllocateInfo cai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cai.commandPool = fr.pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    if (!VK_OK(vkAllocateCommandBuffers(vk.device, &cai, &fr.cmd), "command buffer")) goto fail;
    VkFenceCreateInfo fci = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (!VK_OK(vkCreateFence(vk.device, &fci, NULL, &fr.fence), "fence")) goto fail;
    {
        VkSemaphoreTypeCreateInfo st = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
        st.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        VkSemaphoreCreateInfo sci2 = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        sci2.pNext = &st;
        if (!VK_OK(vkCreateSemaphore(vk.device, &sci2, NULL, &vd.timeline), "timeline semaphore")) goto fail;
        // M6: the depth stage thread waits on it for its readbacks
        ctx->gpuTimeline = vd.timeline;
    }

    // The shader passes (both video kinds through the warp, as M2)
    MlxrPassesInfo pi;
    memset(&pi, 0, sizeof(pi));
    pi.phys = vk.phys;
    pi.device = vk.device;
    pi.nv12 = 1;
    pi.nv12Chroma = MLXR_CHROMA_LEFT;
    // The room's atlas at a glancing angle (upstream's roomAnisotropy cap;
    // xr_vk enables samplerAnisotropy)
    pi.atlasAnisotropy = ROOM_ANISOTROPY_MAX;
    if (mlxr_passes_create(&pi, &passes) != 0) {
        LOGE("shader passes unavailable");
        passes = NULL;
        goto fail;
    }
    ctx->passes = passes;
    xrWorldFindAssets(ctx);
    {
        VkCommandBuffer cmd = frameBegin(ctx, &fr);
        mlxr_passes_prepare(passes, cmd);
        vkEndCommandBuffer(cmd);
        fr.recording = 0;
        VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        vkResetFences(vk.device, 1, &fr.fence);
        vkQueueSubmit(vk.queue, 1, &si, fr.fence);
    }

    // The UI: one atlas swapchain and the panels drawn into it
    if (!xrAtlasCreate(ctx) || !xrPanelsCreate(ctx)) {
        LOGE("UI atlas unavailable");
        goto fail;
    }
    // Native: the panels are drawn on a worker thread (a cog redraw is 4-7 ms,
    // the keyboard ~10 ms: too much of a 90 Hz frame). MLXR_PANELS_INLINE=1
    // draws them on the frame loop as before.
    {
        const char* inl = getenv("MLXR_PANELS_INLINE");
        if (inl == NULL || inl[0] != '1') xrPanelsSetAsync(ctx);
    }
    LOGEV("swapchains alive %d of 16 at start (the picture's comes with its first frame, passthrough's "
          "when it goes on)", ctx->swapchainsAlive);

    pthread_mutex_lock(&g_reqLock);
    g_running = 1;
    pthread_mutex_unlock(&g_reqLock);

    double start = nowNs() / 1e9, lastStats = start;
    uint64_t frames = 0, framesWindow = 0, videoWindow = 0, videoFrames = 0;
    int64_t endNsSum = 0, endNsMax = 0;
    // Native (lane wave2-bind-wire): the host app last handed to the bindings
    unsigned hostAppSeen = 0;
    // Native (lane av-latency): the parts of the host cursor's delay that are
    // ours, per 5 s window: when the mouse move leaves after xrWaitFrame
    // returns, how far ahead of display the poses are, and how long a decoded
    // picture takes from the decoder to photons
    int latMoves = 0, latLeadN = 0, latVidN = 0;
    // Native (lane perf): 0 newest, 1 smooth (moonlight-qt's Pacer), 2 or
    // unset low latency (pacer.h)
    const char* pacingEnv = getenv("MLXR_VIDEO_PACING");
    MlxrPaceMode paceMode = mlxr_pace_mode_parse(pacingEnv);
    HwVideoStats pacePrev;
    memset(&pacePrev, 0, sizeof(pacePrev));
    LOGEV("video frame pacing %s (MLXR_VIDEO_PACING=%s: 0 newest, 1 smooth, 2 low latency)",
          mlxr_pace_mode_name(paceMode), pacingEnv ? pacingEnv : "unset");
    int64_t latSendNsSum = 0, latSendNsMax = 0, latLeadNsSum = 0, latVidNsSum = 0, latVidNsMax = 0;
    // Native (lane perf): the last stage of a picture: taken from the queue
    // to its predicted display time, and how far into the frame the take is
    // (after xrWaitFrame returned)
    int64_t latShowNsSum = 0, latShowNsMax = 0, latTakeNsSum = 0, latTakeNsMax = 0;
    int exitAsked = 0;
    while (!ctx->exitRequested) {
        if ((atomic_load(&g_quit) || (cfg->seconds > 0 && nowNs() / 1e9 - start > cfg->seconds)) && !exitAsked) {
            exitAsked = 1;
            if (ctx->sessionRunning) {
                LOGEV("requesting session exit");
                xrRequestExitSession(ctx->session);
            }
            else {
                break;
            }
        }
        pollEvents(ctx, &cfg->hooks);
        if (ctx->exitRequested) break;

        // A stream stopped: let go of every frame and import of its decoder
        pthread_mutex_lock(&g_reqLock);
        int release = g_releaseAsked;
        g_releaseAsked = 0;
        unsigned serial = g_releaseSerial;
        int nNotices = g_noticeCount;
        char notices[4][160];
        memcpy(notices, g_notices, sizeof(notices));
        g_noticeCount = 0;
        pthread_mutex_unlock(&g_reqLock);
        for (int i = 0; i < nNotices; i++) xrPanelsNotice(ctx, notices[i]);
        if (release) {
            vkWaitForFences(vk.device, 1, &fr.fence, VK_TRUE, UINT64_MAX);
            hwDestroy(&vk, &vd);
            vd.swValid = 0;
            vd.draws = 0;
            ctx->videoShown = 0;
            AVFrame* stale = cfg->video ? frame_slot_take(cfg->video) : NULL;
            av_frame_free(&stale);
            xrWorldVideoGone(ctx);
            LOGEV("video released (stream stopped)");
            pthread_mutex_lock(&g_reqLock);
            g_releaseDone = serial;
            pthread_cond_broadcast(&g_reqDone);
            pthread_mutex_unlock(&g_reqLock);
        }

        if (!ctx->sessionRunning) {
            struct timespec ts = { 0, 10000000 };
            nanosleep(&ts, NULL);
            continue;
        }

        XrFrameState fs = { XR_TYPE_FRAME_STATE };
        if (!checkXr(xrWaitFrame(ctx->session, NULL, &fs), "xrWaitFrame")) break;
        int64_t waitedNs = nowNs();
        if (!checkXr(xrBeginFrame(ctx->session, NULL), "xrBeginFrame")) break;
        ctx->predictedDisplayTime = fs.predictedDisplayTime;
        ctx->shouldRender = fs.shouldRender;
        displayFrameBegun(ctx, &fs);
        frames++;
        framesWindow++;
        int64_t now = nowNs();

        // The last frame's GPU work is done: the atlas copy and the staging
        // buffers are free to write
        vkWaitForFences(vk.device, 1, &fr.fence, VK_TRUE, UINT64_MAX);
        ctx->gpuDone = vd.value;

        // ---- input (nativeUpdateInput) and what it decided (dispatchInput)
        host.live = *cfg->settings;
        takeHostApp(ctx, &hostAppSeen);
        if (atomic_exchange(&g_kbToggle, 0)) {
            xrKeyboardToggle(ctx, "a gamepad's Y");
        }
        xrUpdateInput(ctx, host.distance, host.quadWidth, host.curvature, host.live.head_locked,
                      host.live.pointer, host.live.gaze && ctx->eyeGaze, host.live.pointer_sleep, host.out);
        if (cfg->hooks.headYaw) cfg->hooks.headYaw(cfg->hooks.user, host.out[IN_HEAD_YAW]);
        XrUiEvents ev;
        memset(&ev, 0, sizeof(ev));
        int64_t sampledNs = nowNs();
        host.pointerSent = 0;
        dispatchInput(&host, &ev);
        int64_t displayMonoNs = xrTimeToMonoNs(ctx->instance, fs.predictedDisplayTime);
        if (host.pointerSent) {
            int64_t took = nowNs() - waitedNs;
            latMoves++;
            latSendNsSum += took;
            if (took > latSendNsMax) latSendNsMax = took;
        }
        if (displayMonoNs > sampledNs) {
            latLeadNsSum += displayMonoNs - sampledNs;
            latLeadN++;
        }
        ev.pictureShown = ctx->videoShown;
        // M5: the click on a panel press (upstream XrRenderer.updateClickSound)
        if (ctx->clickSoundOn) {
            clickStart();
        }
        xrClickFrame(ctx, host.out);
        XrUiLauncher show;
        memset(&show, 0, sizeof(show));
        if (cfg->hooks.ui) cfg->hooks.ui(cfg->hooks.user, &ev, &show);
        else if (ev.exitConfirmed || ev.disconnectConfirmed || ev.pcvrConfirmed) atomic_store(&g_quit, 1);
        XrLauncherView lv;
        memset(&lv, 0, sizeof(lv));
        lv.mode = show.mode;
        lv.pcs = show.pcs;
        lv.pcCount = show.pcCount;
        lv.pcName = show.pcName;
        lv.apps = show.apps;
        lv.appCount = show.appCount;
        lv.page = show.page;
        lv.loading = show.loading;
        lv.failed = show.failed;
        ctx->streaming = atomic_load(&g_streaming);

        // ---- the panels' art, from what config.ini and the context say now
        xrSettingsFromCtx(ctx, &host.live);
        host.live.head_locked = cfg->settings->head_locked;
        updateTexts(&host, now, frames, videoFrames);
        xrPanelsUpdate(ctx, &host.live, &lv, host.statsText, host.clockText, now);
        if (ctx->launcher != LAUNCHER_NONE || ctx->videoShown) ctx->everRendered = 1;

        // ---- the GPU work of this frame, one submission
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        int atlasAcquired = 0;
        if (fs.shouldRender) {
            cmd = frameBegin(ctx, &fr);
            if (xrAtlasRecord(ctx, cmd, &atlasAcquired) < 0) {
                LOGW("UI atlas upload skipped this frame");
            }
            // M5: the room, the glow and the models record into the same
            // buffer; everything up to vd.value has finished (the fence above)
            ctx->frameCmd = cmd;
            ctx->gpuSerial = vd.value + 1;
            xrWorldFrameBegin(ctx);
        }
        // M6: the separation the preference holds, then the depth model's
        // lifecycle, its rate and the newest map into a depth texture
        ctx->separationPref = separationOf(settings_separation_units(cfg->settings));
        xrDepthFrame(ctx);

        // The hardware decoder of the stream, if one is up
        HwVideo* hv = hw_video_current();
        if (hv != vd.src) {
            vkWaitForFences(vk.device, 1, &fr.fence, VK_TRUE, UINT64_MAX);
            hwDestroy(&vk, &vd);
            if (hv) {
                mlxr_vkimp_config ic;
                memset(&ic, 0, sizeof(ic));
                ic.phys = vk.phys;
                ic.device = vk.device;
                ic.queue_family = vk.queueFamily;
                ic.release = hwReleaseCb;
                ic.release_user = hv;
                ic.linear_filter = true;
                if (mlxr_vkimp_create(&ic, &vd.imp) == 0) {
                    vd.src = hv;
                    LOGEV("hardware video: importer ready");
                }
                else {
                    LOGE("hardware video: importer failed");
                    vd.imp = NULL;
                    vd.src = hv;
                }
            }
        }
        uint32_t videoIdx = 0;
        int videoAcquired = 0, swapAcquired = 0, heldSampled = 0;
        // Native: a curved picture goes through the app's own image and the
        // panel pass (no cylinder layers on SteamVR)
        // and, natively, the flat picture in 3D: SteamVR shows no quad layer
        // seen by one eye only, so both eyes' halves go up in this projection
        // layer instead of upstream's two quads
        int curvedNow = xrPictureProjected(ctx) && !curve.failed;
        // M5: or supersampled, through an image of its own and the
        // supersample pass into a swapchain the size it shows at
        int ssNow = fs.shouldRender && superWanted(ctx, &vd, &ss, curvedNow);
        XrCurve* own = curvedNow ? &curve : ssNow ? &ss.pic : NULL;
        int ownKey = curvedNow ? 1 : ssNow ? 2 + ((ss.wantW << 4) | ctx->supersampleMode) : 0;
        ss.pic.mode = ownKey;
        uint64_t signal = 0;
        int hwNew = 0;
        uint64_t shownSubmitNs = 0; // Native (av-latency): when the picture drawn now went to the decoder
        int64_t takenNs = 0;        // Native (lane perf): when it left the queue
        mlxr_vframe hf;
        if (vd.imp) {
            mlxr_vkimp_poll(vd.imp);
            if (hw_video_format_change_pending(hv)) {
                LOGEV("hardware video: resolution change, dropping imports");
                vkWaitForFences(vk.device, 1, &fr.fence, VK_TRUE, UINT64_MAX);
                hwDropImports(&vk, &vd);
                hw_video_format_change_done(hv);
            }
            // Native (lane av-latency): paced (oldest queued frame each refresh,
            // moonlight-qt's Pacer) unless MLXR_VIDEO_PACING=0 (newest frame)
            int64_t takeNs = nowNs();
            if (hw_video_take_refresh(hv, paceMode, (int)(xrGetDisplayRate(ctx) + 0.5f), (uint64_t)takeNs, &hf)) {
                const mlxr_vkimp_image* img = fs.shouldRender && cmd ? mlxr_vkimp_get(vd.imp, &hf) : NULL;
                VkImageView nv12 = img ? hwView(&vk, &vd, passes, img) : VK_NULL_HANDLE;
                if (!nv12 || videoSwapchain(ctx, &vd, (int)hf.width, (int)hf.height) != 0) {
                    hw_video_release(hv, &hf);
                }
                else {
                    if (videoTarget(ctx, &vd, own, own != NULL, &videoIdx, &swapAcquired)) {
                        videoAcquired = 1;
                        mlxr_vkimp_cmd_acquire(vd.imp, cmd, img, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
                        MlxrVideo mv;
                        memset(&mv, 0, sizeof(mv));
                        mv.kind = MLXR_VIDEO_NV12;
                        mv.planes[0] = nv12;
                        mv.width = hf.width;
                        mv.height = hf.height;
                        mv.texWidth = hf.coded_width;
                        mv.texHeight = hf.coded_height;
                        recordWarp(ctx, &vd, passes, cmd, videoIdx, &mv, own, 1, videoFrames);
                        if (ssNow) superRecord(ctx, &ss, passes, cmd, vd.format);
                        xrGpuTimeEnd(ctx, GPU_WARP);
                        // M5: the colour sample, the glow, the letterbox
                        // detection, from the same frame
                        xrWorldVideoFrame(ctx, &mv);
                        mlxr_vkimp_cmd_release(vd.imp, cmd, img, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
                        hwNew = 1;
                        shownSubmitNs = hf.t_submit_ns;
                        takenNs = takeNs;
                    }
                    else {
                        hw_video_release(hv, &hf);
                    }
                }
            }
        }

        // A software frame (the --sw-decode stream, or --file with it)
        AVFrame* f = cfg->video ? frame_slot_take(cfg->video) : NULL;
        if (f && !videoAcquired && fs.shouldRender && cmd) {
            if (swPlanes(&vk, &vd, f) == 0 && videoSwapchain(ctx, &vd, f->width, f->height) == 0) {
                if (videoTarget(ctx, &vd, own, own != NULL, &videoIdx, &swapAcquired)) {
                    videoAcquired = 1;
                    swUpload(&vd, cmd, f);
                    MlxrVideo mv;
                    memset(&mv, 0, sizeof(mv));
                    mv.kind = MLXR_VIDEO_YUV420P;
                    mv.planes[0] = vd.planes[0].view;
                    mv.planes[1] = vd.planes[1].view;
                    mv.planes[2] = vd.planes[2].view;
                    mv.width = (uint32_t)f->width;
                    mv.height = (uint32_t)f->height;
                    mv.chroma = MLXR_CHROMA_LEFT;
                    recordWarp(ctx, &vd, passes, cmd, videoIdx, &mv, own, 1, videoFrames);
                        if (ssNow) superRecord(ctx, &ss, passes, cmd, vd.format);
                    xrGpuTimeEnd(ctx, GPU_WARP);
                    xrWorldVideoFrame(ctx, &mv);
                }
            }
        }
        av_frame_free(&f);

        // The grade or the sharpening moved on a picture standing still: the
        // frame in hand is drawn again (upstream's warpRedraw); the glow's
        // switch or level moved: the glow is (upstream's redrawGlow)
        int redrawPicture = !videoAcquired && vd.swapchain && videoStale(ctx, &vd, ownKey);
        int redrawGlowNow = !videoAcquired && ctx->ambiReady && glowStale(ctx) && ctx->videoShown;
        if ((redrawPicture || redrawGlowNow) && fs.shouldRender && cmd && (vd.heldValid || vd.swValid)) {
            MlxrVideo mv;
            memset(&mv, 0, sizeof(mv));
            const mlxr_vkimp_image* img = NULL;
            int have = 0;
            if (vd.heldValid) {
                img = mlxr_vkimp_get(vd.imp, &vd.held);
                VkImageView nv12 = img ? hwView(&vk, &vd, passes, img) : VK_NULL_HANDLE;
                if (nv12) {
                    mlxr_vkimp_cmd_acquire(vd.imp, cmd, img, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
                    mv.kind = MLXR_VIDEO_NV12;
                    mv.planes[0] = nv12;
                    mv.width = vd.held.width;
                    mv.height = vd.held.height;
                    mv.texWidth = vd.held.coded_width;
                    mv.texHeight = vd.held.coded_height;
                    have = 1;
                    heldSampled = 1;
                }
            }
            else {
                mv.kind = MLXR_VIDEO_YUV420P;
                mv.planes[0] = vd.planes[0].view;
                mv.planes[1] = vd.planes[1].view;
                mv.planes[2] = vd.planes[2].view;
                mv.width = (uint32_t)vd.planeW;
                mv.height = (uint32_t)vd.planeH;
                mv.chroma = MLXR_CHROMA_LEFT;
                have = 1;
            }
            if (have && redrawPicture && videoTarget(ctx, &vd, own, own != NULL, &videoIdx, &swapAcquired)) {
                videoAcquired = 1;
                recordWarp(ctx, &vd, passes, cmd, videoIdx, &mv, own, 0, videoFrames);
                        if (ssNow) superRecord(ctx, &ss, passes, cmd, vd.format);
                xrGpuTimeEnd(ctx, GPU_WARP);
                LOGI("picture drawn again on a still frame (grade %s, sharpen %d, %d eyes)", ctx->gradeOn ? "on" : "off",
                     ctx->sharpenMode, ctx->drawnEyes);
            }
            if (have && redrawGlowNow) {
                xrWorldStillFrame(ctx, &mv);
            }
            if (img != NULL && have) {
                mlxr_vkimp_cmd_release(vd.imp, cmd, img, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
            }
        }

        // M5: where the controllers are this frame, before anything draws
        // them; then the room (every display frame while one is up: SteamVR
        // only turns a projection layer with the head between frames, so a
        // room drawn only with new pictures would lag a moving head) and the
        // models (every display frame, as upstream)
        updateControllerModels(ctx);
        // Native: one projection layer a frame; with the picture's up, the room
        // and the models are drawn into it by curveRecord, and without it the
        // models go into the room's (xrWorldPlanLayers)
        xrWorldPlanLayers(ctx, curvedNow && fs.shouldRender && cmd && ctx->launcher == LAUNCHER_NONE
                                       && ctx->videoShown);
        if (fs.shouldRender && cmd) {
            if (roomEffective(ctx) > 0 && !ctx->passthrough && !ctx->roomInPicture) {
                renderRoom(ctx);
            }
            if (ctx->everRendered && !ctx->modelsInPicture && !ctx->modelsInRoom) {
                renderControllerModels(ctx);
            }
        }

        // The curved picture, every display frame (the eyes move)
        int curveAcquired = 0;
        if (curvedNow && fs.shouldRender && cmd && ctx->launcher == LAUNCHER_NONE) {
            XrPosef pp;
            float pw, ph;
            XrSpace space;
            xrPictureView(ctx, &pp, &pw, &ph, &space);
            curveAcquired = curveRecord(ctx, &curve, passes, cmd, vd.format, space, cfg->settings->eye_swap);
        }
        // The flat glow's quad: its swapchain gets the newest glow
        if (fs.shouldRender && cmd) {
            xrWorldFlatGlow(ctx, curvedNow && ctx->launcher == LAUNCHER_NONE);
        }
        if (fr.recording) {
            vkEndCommandBuffer(fr.cmd);
            fr.recording = 0;
            signal = ++vd.value;
            // Uploads the room's loader recorded go first, in the same submission
            VkCommandBuffer cmds[4];
            int ncmd = xrWorldTakeUploads(ctx, cmds, 3);
            cmds[ncmd++] = fr.cmd;
            VkTimelineSemaphoreSubmitInfo tsi = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
            tsi.signalSemaphoreValueCount = 1;
            tsi.pSignalSemaphoreValues = &signal;
            VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
            si.pNext = &tsi;
            si.commandBufferCount = (uint32_t)ncmd;
            si.pCommandBuffers = cmds;
            si.signalSemaphoreCount = 1;
            si.pSignalSemaphores = &vd.timeline;
            vkResetFences(vk.device, 1, &fr.fence);
            if (vkQueueSubmit(vk.queue, 1, &si, fr.fence) != VK_SUCCESS) {
                LOGE("vkQueueSubmit failed");
                break;
            }
            xrWorldSubmitted(ctx, signal);
            xrDepthSubmitted(ctx, signal);
        }
        ctx->frameCmd = VK_NULL_HANDLE;
        xrWorldReleaseImages(ctx);
        if (ss.acquired) {
            XrSwapchainImageReleaseInfo rli = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
            checkXr(xrReleaseSwapchainImage(ss.swapchain, &rli), "release supersampled picture image");
            ss.acquired = 0;
            ss.filled = 1;
        }
        if (hwNew) {
            // The new frame is held for a redraw; the one it replaces goes back
            // once the last submission that sampled it is done
            hwLetGoHeld(&vd);
            vd.held = hf;
            vd.heldValid = 1;
            vd.heldValue = signal;
        }
        else if ((videoAcquired || heldSampled) && vd.heldValid) {
            vd.heldValue = signal;
        }
        if (atlasAcquired) xrAtlasRelease(ctx);
        if (curveAcquired) {
            // Native (lane eye-swapchains): an image of each eye's swapchain
            for (int e = 0; e < ROOM_EYES; e++) {
                XrSwapchainImageReleaseInfo rli = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
                checkXr(xrReleaseSwapchainImage(curve.swapchain[e], &rli), "release curved picture image");
            }
        }
        if (videoAcquired) {
            if (swapAcquired) {
                XrSwapchainImageReleaseInfo rli = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
                checkXr(xrReleaseSwapchainImage(vd.swapchain, &rli), "release video image");
            }
            ctx->videoShown = 1;
            ctx->everRendered = 1;
            videoWindow++;
            videoFrames++;
            if (shownSubmitNs > 0 && displayMonoNs > (int64_t)shownSubmitNs) {
                int64_t v = displayMonoNs - (int64_t)shownSubmitNs;
                if (v < 2000000000LL) {
                    latVidNsSum += v;
                    if (v > latVidNsMax) latVidNsMax = v;
                    latVidN++;
                    int64_t show = takenNs > 0 && displayMonoNs > takenNs ? displayMonoNs - takenNs : 0;
                    int64_t into = takenNs > waitedNs ? takenNs - waitedNs : 0;
                    latShowNsSum += show;
                    if (show > latShowNsMax) latShowNsMax = show;
                    latTakeNsSum += into;
                    if (into > latTakeNsMax) latTakeNsMax = into;
                }
            }
        }

        // ---- the layers (nativeEndFrame) and the end of the frame
        XrFrameIn fin;
        memset(&fin, 0, sizeof(fin));
        fin.distance = host.distance;
        fin.quadWidth = host.quadWidth;
        fin.curvature = host.curvature;
        fin.headLocked = cfg->settings->head_locked;
        fin.eyeSwap = cfg->settings->eye_swap;
        fin.passthrough = ctx->passthrough;
        fin.curved = curveAcquired ? &curve.layer : NULL;
        if (ssNow && ss.filled && ss.w > 0) {
            fin.picture = ss.swapchain;
            fin.pictureW = ss.w;
            fin.pictureH = ss.h;
        }
        XrFrameOut fout;
        memset(&fout, 0, sizeof(fout));
        if (fs.shouldRender) {
            xrBuildLayers(ctx, &fin, &fout);
        }
        xrSplashLayers(ctx, &fout, nowNs()); // Native (lane wave6-splash): the logo until the first frame, while it reconnects
        if (fr.recording) {
            // xrBuildLayers made the passthrough swapchain this frame
            vkEndCommandBuffer(fr.cmd);
            fr.recording = 0;
            VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
            si.commandBufferCount = 1;
            si.pCommandBuffers = &fr.cmd;
            vkResetFences(vk.device, 1, &fr.fence);
            vkQueueSubmit(vk.queue, 1, &si, fr.fence);
        }
        XrFrameEndInfo fei = { XR_TYPE_FRAME_END_INFO };
        fei.displayTime = fs.predictedDisplayTime;
        fei.environmentBlendMode = fs.shouldRender ? fout.blend : XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        fei.layerCount = fout.count;
        fei.layers = fout.order;
        int64_t endBegan = nowNs();
        if (!checkXr(xrEndFrame(ctx->session, &fei), "xrEndFrame")) break;
        // Native: how long the runtime held the frame in xrEndFrame (its own
        // work on the layers, such as downsampling a scene texture too large
        // for the compositor), for the room-judder diagnosis
        int64_t endTook = nowNs() - endBegan;
        endNsSum += endTook;
        if (endTook > endNsMax) endNsMax = endTook;
        displayFrameEnded(ctx);

        double t = nowNs() / 1e9;
        if (t - lastStats >= 5.0) {
            char extra[512] = "";
            if (cfg->hooks.stats) cfg->hooks.stats(cfg->hooks.user, extra, sizeof(extra));
            int panelDraws = 0;
            double panelMs = 0.0;
            xrPanelsStats(ctx, &panelDraws, &panelMs);
            double panelWait = xrPanelsWorstWaitMs(ctx);
            char gpu[320];
            xrGpuTimesLine(ctx, gpu, sizeof(gpu));
            char depth[400];
            xrDepthStatsLine(ctx, depth, sizeof(depth));
            LOGEV("xr: %.1f fps (display %.0f Hz), video %.1f fps drawn, %u layers, swapchains %d of 16, %d panel "
                  "drawings %.1f ms avg (worst wait %.0f ms); room %d%s, glow %s, models %s%s%s%s%s; %s",
                  framesWindow / (t - lastStats), xrGetDisplayRate(ctx), videoWindow / (t - lastStats), fout.count,
                  ctx->swapchainsAlive, panelDraws, panelMs, panelWait, roomEffective(ctx),
                  roomEffective(ctx) > 0 && xrRoomResident(ctx) != roomEffective(ctx) ? " (loading)" : "",
                  xrWorldGlowShown(ctx) ? "up" : "off", ctx->modelsShowing ? "up" : "off", depth[0] ? "; " : "",
                  depth, gpu[0] ? "; " : "", gpu, extra);
            int roomDrawn = 0, roomStale = 0;
            xrRoomTakeCounts(&roomDrawn, &roomStale);
            char byDevice[256];
            gpuByDeviceLine(byDevice, sizeof(byDevice), t - lastStats);
            LOGEV("xr: timing: xrEndFrame %.2f ms avg (max %.2f) over %llu frames; room drawn on %d, kept an older "
                  "image on %d; GPU by device in this process: %s",
                  framesWindow ? endNsSum / 1e6 / (double)framesWindow : 0.0, endNsMax / 1e6,
                  (unsigned long long)framesWindow, roomDrawn, roomStale, byDevice);
            // Native (lane wave3-hud): the compositor's own frame times and drops, beside ours
            {
                MlxrCompositorMetrics cm;
                char compLine[96];
                metricsEnsure(&host);
                if (xrMetricsSample(ctx, &cm)) {
                    mlxr_compositor_line(&cm, compLine, sizeof(compLine));
                    if (cm.haveApp) {
                        LOGEV("xr: %s; app cpu %.2f ms, gpu %.2f ms (runtime's count)", compLine[0] ? compLine : "compositor: no figures",
                              (double)cm.appCpuMs, (double)cm.appGpuMs);
                    }
                    else if (compLine[0]) {
                        LOGEV("xr: %s", compLine);
                    }
                }
            }
            // Native (lane av-latency): what of the host cursor's delay and of the
            // audio's underruns is ours, for the next owner run
            char appLat[1600] = "";
            if (cfg->hooks.latency) cfg->hooks.latency(cfg->hooks.user, appLat, sizeof(appLat));
            // Native (lane av-latency): how evenly the stream's frames reached
            // the display refreshes in this window
            char pacing[700] = "";
            HwVideo* hvNow = hw_video_current();
            if (hvNow) {
                HwVideoStats hs;
                hw_video_stats(hvNow, &hs);
                if (hs.refreshes < pacePrev.refreshes || hs.frames < pacePrev.frames) memset(&pacePrev, 0, sizeof(pacePrev));
                uint64_t refr = hs.refreshes - pacePrev.refreshes;
                // Native (lane perf): the decoder-side stages of the frames
                // taken, and the AUs in the decoder at each submit
                uint64_t timed = hs.takenTimed - pacePrev.takenTimed;
                uint64_t inN = hs.inFlightSamples - pacePrev.inFlightSamples;
                char stages[320];
                snprintf(stages, sizeof(stages),
                         "decode %.1f ms (max %.1f), waited in the queue %.1f ms (max %.1f), AUs in the decoder %.2f avg "
                         "(max %llu), %llu stale after a stall skipped, latched for the decoder %llu times (%llu got the "
                         "picture, %.1f ms avg); ",
                         timed ? (hs.decodeNsSum - pacePrev.decodeNsSum) / 1e6 / (double)timed : 0.0,
                         hs.decodeNsMax / 1e6, timed ? (hs.waitNsSum - pacePrev.waitNsSum) / 1e6 / (double)timed : 0.0,
                         hs.waitNsMax / 1e6, inN ? (double)(hs.inFlightSum - pacePrev.inFlightSum) / (double)inN : 0.0,
                         (unsigned long long)hs.inFlightMax, (unsigned long long)(hs.staleDrops - pacePrev.staleDrops),
                         (unsigned long long)(hs.latchWaits - pacePrev.latchWaits),
                         (unsigned long long)(hs.latchHits - pacePrev.latchHits),
                         hs.latchWaits > pacePrev.latchWaits
                             ? (hs.latchNsSum - pacePrev.latchNsSum) / 1e6 / (double)(hs.latchWaits - pacePrev.latchWaits)
                             : 0.0);
                hw_video_reset_max(hvNow);
                snprintf(pacing, sizeof(pacing),
                         "frames %s: %llu decoded, %llu new of %llu refreshes, %llu one-refresh repeats, %llu catch-up "
                         "drops, %llu replaced/overflow, %llu flagged damaged, %.2f waiting avg; %s",
                         paceMode == MLXR_PACE_NEWEST ? "newest" : paceMode == MLXR_PACE_SMOOTH ? "paced" : "paced low latency",
                         (unsigned long long)(hs.frames - pacePrev.frames),
                         (unsigned long long)(hs.shown - pacePrev.shown), (unsigned long long)refr,
                         (unsigned long long)(hs.holes - pacePrev.holes),
                         (unsigned long long)(hs.catchUpDrops - pacePrev.catchUpDrops),
                         (unsigned long long)(hs.skipped - pacePrev.skipped),
                         (unsigned long long)(hs.corrupt - pacePrev.corrupt),
                         refr ? (double)(hs.queuedSum - pacePrev.queuedSum) / (double)refr : 0.0, stages);
                pacePrev = hs;
            }
            if (atomic_load(&g_streaming)) {
                LOGEV("xr: latency: %spointer %d moves (%.0f/s), sent %.1f ms after xrWaitFrame returned (max %.1f), "
                      "poses %.0f ms before display, smoothing lag %.0f ms (%.1f px) while moving (%d frames); "
                      "decoder submit to photons %.0f ms avg (max %.0f) over %d frames, of which taken to photons %.1f ms "
                      "(max %.1f), taken %.1f ms after xrWaitFrame returned (max %.1f)",
                      pacing, latMoves, latMoves / (t - lastStats), latMoves ? latSendNsSum / 1e6 / latMoves : 0.0,
                      latSendNsMax / 1e6, latLeadN ? latLeadNsSum / 1e6 / latLeadN : 0.0,
                      ctx->ptrLagSamples ? ctx->ptrLagMsSum / ctx->ptrLagSamples : 0.0,
                      ctx->ptrLagSamples ? ctx->ptrLagPxSum / ctx->ptrLagSamples : 0.0, ctx->ptrLagSamples,
                      latVidN ? latVidNsSum / 1e6 / latVidN : 0.0, latVidNsMax / 1e6, latVidN,
                      latVidN ? latShowNsSum / 1e6 / latVidN : 0.0, latShowNsMax / 1e6,
                      latVidN ? latTakeNsSum / 1e6 / latVidN : 0.0, latTakeNsMax / 1e6);
                if (appLat[0]) LOGEV("xr: latency (stream): %s", appLat);
            }
            latMoves = latLeadN = latVidN = 0;
            latSendNsSum = latSendNsMax = latLeadNsSum = latVidNsSum = latVidNsMax = 0;
            latShowNsSum = latShowNsMax = latTakeNsSum = latTakeNsMax = 0;
            ctx->ptrLagMsSum = ctx->ptrLagPxSum = 0.0;
            ctx->ptrLagSamples = 0;
            endNsSum = endNsMax = 0;
            framesWindow = videoWindow = 0;
            lastStats = t;
        }
    }
    rc = 0;
    LOGEV("session ended after %llu frames", (unsigned long long)frames);

fail:
    pthread_mutex_lock(&g_reqLock);
    g_running = 0;
    g_releaseDone = g_releaseSerial;
    pthread_cond_broadcast(&g_reqDone);
    pthread_mutex_unlock(&g_reqLock);
    if (vk.device) {
        vkDeviceWaitIdle(vk.device);
        // M6: the stage thread joined and the model stopped before anything
        // they read goes
        xrDepthDestroy(ctx);
        xrWorldDestroy(ctx);
        hwDestroy(&vk, &vd);
        freeSwPlanes(&vk, &vd);
        freeVideoSwapchain(ctx, &vd);
        curveDestroy(ctx, &curve);
        superDestroy(ctx, &ss);
        xrPanelsDestroy(ctx);
        xrAtlasDestroy(ctx);
        if (ctx->passthroughSwapchain) xrDestroySwapchain(ctx->passthroughSwapchain);
        if (passes) mlxr_passes_destroy(passes);
        if (vd.timeline) vkDestroySemaphore(vk.device, vd.timeline, NULL);
        if (fr.fence) vkDestroyFence(vk.device, fr.fence, NULL);
        if (fr.pool) vkDestroyCommandPool(vk.device, fr.pool, NULL);
    }
    clickStop();
    destroyXrInput(ctx);
    if (ctx->viewSpace) xrDestroySpace(ctx->viewSpace);
    if (ctx->localSpace) xrDestroySpace(ctx->localSpace);
    perfClose(&host);
    xrMetricsStop(ctx);
    if (ctx->session) xrDestroySession(ctx->session);
    if (ctx->instance) xrDestroyInstance(ctx->instance);
    xr_vk_destroy(&vk);
    free(ctx);
    return rc;
}
