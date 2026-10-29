// Moonlight XR Native: headless Vulkan tests for the rooms lane. Usage:
//   mlxr-room-test <group> <outdir> [converted Frame models dir]
// groups: logic load rescale render_<room> model overlay model_textured
// model_fallback all. Images are written as PNG into <outdir>. GPLv3.
#include <math.h>
#include <png.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "passes.h"
#include "room_gpu.h"
#include "room_logic.h"
#include "xr_atlas.h"
#include "xr_controller.h"

#ifndef MLXR_ASSETS_DIR
#define MLXR_ASSETS_DIR "assets"
#endif

static int failed, run;
static const char* outDir = ".";

static void check(int ok, const char* fmt, ...) {
    run++;
    va_list ap;
    va_start(ap, fmt);
    if (!ok) {
        failed++;
        fprintf(stderr, "FAIL: ");
        vfprintf(stderr, fmt, ap);
        fprintf(stderr, "\n");
    }
    va_end(ap);
}

static void note(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}

static double nowMs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}

#define NEAR(a, b, tol) (fabs((double)(a) - (double)(b)) <= (tol))

// ------------------------------------------------------------ Vulkan device

static MlxrGpu gpu;
static VkInstance instance;
static VkCommandPool pool;
static VkPhysicalDeviceProperties props;
static MlxrPasses* passes;
// Native: textureCompressionASTC_LDR is on. The rooms' atlases are ASTC; a
// device without it (lavapipe) skips the room groups, the model groups run
static int astcOk;
static int astcSkipped;

static int needAstc(void) {
    if (!astcOk) astcSkipped++;
    return astcOk;
}

static int vkInit(void) {
    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "mlxr-room-test";
    app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    // 77: no Vulkan driver or device at all (a CI runner); ctest counts it as skipped
    if (vkCreateInstance(&ici, NULL, &instance) != VK_SUCCESS) {
        return 77;
    }
    uint32_t n = 0;
    VkPhysicalDevice devs[8];
    vkEnumeratePhysicalDevices(instance, &n, NULL);
    if (n == 0) return 77;
    if (n > 8) n = 8;
    vkEnumeratePhysicalDevices(instance, &n, devs);
    gpu.phys = devs[0];
    vkGetPhysicalDeviceProperties(gpu.phys, &props);
    uint32_t nq = 0;
    VkQueueFamilyProperties qf[16];
    vkGetPhysicalDeviceQueueFamilyProperties(gpu.phys, &nq, NULL);
    if (nq > 16) nq = 16;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu.phys, &nq, qf);
    gpu.queueFamily = UINT32_MAX;
    for (uint32_t i = 0; i < nq; i++)
        if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            gpu.queueFamily = i;
            break;
        }
    if (gpu.queueFamily == UINT32_MAX) return -1;

    VkPhysicalDeviceFeatures have;
    vkGetPhysicalDeviceFeatures(gpu.phys, &have);
    astcOk = have.textureCompressionASTC_LDR == VK_TRUE;
    if (!astcOk) {
        fprintf(stderr, "no textureCompressionASTC_LDR on %s: the room groups skip\n", props.deviceName);
    }
    const char* exts[] = {VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME};
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = gpu.queueFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    VkPhysicalDeviceDynamicRenderingFeaturesKHR dr = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR};
    dr.dynamicRendering = VK_TRUE;
    VkPhysicalDeviceFeatures2 f2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    f2.pNext = &dr;
    f2.features.samplerAnisotropy = have.samplerAnisotropy;
    f2.features.textureCompressionASTC_LDR = have.textureCompressionASTC_LDR;
    VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.pNext = &f2;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 2;
    dci.ppEnabledExtensionNames = exts;
    if (vkCreateDevice(gpu.phys, &dci, NULL, &gpu.device) != VK_SUCCESS) return -1;
    vkGetDeviceQueue(gpu.device, gpu.queueFamily, 0, &gpu.queue);
    VkCommandPoolCreateInfo pci = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = gpu.queueFamily;
    if (vkCreateCommandPool(gpu.device, &pci, NULL, &pool) != VK_SUCCESS) return -1;

    MlxrPassesInfo pi = {0};
    pi.phys = gpu.phys;
    pi.device = gpu.device;
    pi.atlasAnisotropy = have.samplerAnisotropy ? ROOM_ANISOTROPY_MAX : 0.0f;
    if (mlxr_passes_create(&pi, &passes) != 0) return -1;
    return 0;
}

static VkCommandBuffer begin(void) {
    VkCommandBufferAllocateInfo cai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(gpu.device, &cai, &cmd);
    VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    return cmd;
}

static int submit(VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    VkFenceCreateInfo fci = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence;
    vkCreateFence(gpu.device, &fci, NULL, &fence);
    VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    int rc = -1;
    if (vkQueueSubmit(gpu.queue, 1, &si, fence) == VK_SUCCESS)
        rc = vkWaitForFences(gpu.device, 1, &fence, VK_TRUE, 30000000000ull) == VK_SUCCESS ? 0 : -1;
    vkDestroyFence(gpu.device, fence, NULL);
    vkFreeCommandBuffers(gpu.device, pool, 1, &cmd);
    return rc;
}

static uint32_t memType(uint32_t bits, VkMemoryPropertyFlags want) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(gpu.phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) return i;
    return 0;
}

typedef struct {
    VkImage image;
    VkDeviceMemory mem;
    VkImageView view;
    uint32_t w, h;
    VkFormat format;
    VkImageAspectFlags aspect;
} Img;

static void imgMake(Img* im, uint32_t w, uint32_t h, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect) {
    memset(im, 0, sizeof(*im));
    VkImageCreateInfo ci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = format;
    ci.extent = (VkExtent3D){w, h, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = usage;
    vkCreateImage(gpu.device, &ci, NULL, &im->image);
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(gpu.device, im->image, &req);
    VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = memType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(gpu.device, &ai, NULL, &im->mem);
    vkBindImageMemory(gpu.device, im->image, im->mem, 0);
    VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = im->image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    vi.subresourceRange = (VkImageSubresourceRange){aspect, 0, 1, 0, 1};
    vkCreateImageView(gpu.device, &vi, NULL, &im->view);
    im->w = w;
    im->h = h;
    im->format = format;
    im->aspect = aspect;
}

static void imgFree(Img* im) {
    vkDestroyImageView(gpu.device, im->view, NULL);
    vkDestroyImage(gpu.device, im->image, NULL);
    vkFreeMemory(gpu.device, im->mem, NULL);
    memset(im, 0, sizeof(*im));
}

typedef struct {
    VkBuffer buf;
    VkDeviceMemory mem;
    void* map;
} Host;

static void hostMake(Host* h, uint64_t size, VkBufferUsageFlags usage) {
    VkBufferCreateInfo bi = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size;
    bi.usage = usage;
    vkCreateBuffer(gpu.device, &bi, NULL, &h->buf);
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(gpu.device, h->buf, &req);
    VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex =
        memType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(gpu.device, &ai, NULL, &h->mem);
    vkBindBufferMemory(gpu.device, h->buf, h->mem, 0);
    vkMapMemory(gpu.device, h->mem, 0, VK_WHOLE_SIZE, 0, &h->map);
}

static void hostFree(Host* h) {
    vkDestroyBuffer(gpu.device, h->buf, NULL);
    vkFreeMemory(gpu.device, h->mem, NULL);
}

// Same layout, write -> write on a colour attachment between two passes
static void colorBarrier(VkCommandBuffer cmd, VkImage image) {
    VkImageMemoryBarrier b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    b.oldLayout = b.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         0, 0, NULL, 0, NULL, 1, &b);
}

// An RGBA8 UNORM texture, filled from pixels, left SHADER_READ_ONLY
static void texMake(Img* im, uint32_t w, uint32_t h, const uint8_t* px) {
    imgMake(im, w, h, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);
    Host st;
    hostMake(&st, (uint64_t)w * h * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    memcpy(st.map, px, (size_t)w * h * 4);
    VkCommandBuffer cmd = begin();
    mlxr_barrier(cmd, im->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy rg = {0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {w, h, 1}};
    vkCmdCopyBufferToImage(cmd, st.buf, im->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &rg);
    mlxr_barrier(cmd, im->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    submit(cmd);
    hostFree(&st);
}

// ----------------------------------------------------------------- PNG out

static void writePng(const char* name, const uint8_t* rgba, uint32_t w, uint32_t h) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.png", outDir, name);
    png_image img;
    memset(&img, 0, sizeof(img));
    img.version = PNG_IMAGE_VERSION;
    img.width = w;
    img.height = h;
    img.format = PNG_FORMAT_RGBA;
    if (!png_image_write_to_file(&img, path, 0, rgba, 0, NULL)) fprintf(stderr, "png write failed: %s\n", path);
    else note("wrote %s (%ux%u)", path, w, h);
}

// ------------------------------------------------------------- rendering

typedef struct {
    uint32_t eyeW, eyeH;
    int eyes;
    uint8_t* rgba;  // eyes*eyeW x eyeH
    float* depth;   // same size, or NULL
} Frame;

static void frameFree(Frame* f) {
    free(f->rgba);
    free(f->depth);
    memset(f, 0, sizeof(*f));
}

// Square eye, 50 degrees each way, per eye 32 mm from the origin: the head at
// the room's anchor, looking down -Z. Upstream draws from the located views;
// this is the pose a seated viewer starts with.
static void eyeView(int eye, float farZ, float* viewProj) {
    XrFovf fov = {-0.87266f, 0.87266f, 0.87266f, -0.87266f};
    XrPosef pose = {{0, 0, 0, 1}, {eye == 0 ? -0.032f : 0.032f, 0, 0}};
    roomEyeViewProj(viewProj, fov, pose, farZ);
}

static float srgbToLinear(float c) {
    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

static float dimOverride; // 0: the room's own row
static Img ambiImg, shotImg;
static int sharedMade;

static void makeShared(void) {
    if (sharedMade) return;
    sharedMade = 1;
    VkCommandBuffer cmd = begin();
    mlxr_passes_prepare(passes, cmd);
    submit(cmd);
    // A bright, slightly blue colour sample for the spill
    uint8_t ambi[32 * 32 * 4];
    for (int i = 0; i < 32 * 32; i++) {
        ambi[i * 4] = 200;
        ambi[i * 4 + 1] = 215;
        ambi[i * 4 + 2] = 255;
        ambi[i * 4 + 3] = 255;
    }
    texMake(&ambiImg, 32, 32, ambi);
    // A 16:9 test picture: colour bars over a ramp
    const uint32_t W = 320, H = 180;
    uint8_t* shot = malloc((size_t)W * H * 4);
    static const uint8_t bars[8][3] = {{235, 235, 235}, {235, 235, 16}, {16, 235, 235}, {16, 235, 16},
                                       {235, 16, 235}, {235, 16, 16}, {16, 16, 235}, {16, 16, 16}};
    for (uint32_t y = 0; y < H; y++)
        for (uint32_t x = 0; x < W; x++) {
            uint8_t* p = shot + ((size_t)y * W + x) * 4;
            if (y < H * 2 / 3) memcpy(p, bars[x * 8 / W], 3);
            else p[0] = p[1] = p[2] = (uint8_t)(x * 255 / (W - 1));
            if (x < 3 || y < 3 || x >= W - 3 || y >= H - 3) p[0] = p[1] = p[2] = 255;
            p[3] = 255;
        }
    texMake(&shotImg, W, H, shot);
    free(shot);
}

// Renders a room at the default viewpoint into a frame. screenPicture draws
// the test picture where the room hangs it; lit uses the colour sample.
static int renderRoom(const MlxrRoom* room, uint32_t eyeW, uint32_t eyeH, int eyes, int screenPicture, int lit, Frame* f) {
    makeShared();
    memset(f, 0, sizeof(*f));
    const uint32_t W = eyeW * eyes;
    Img color, depth;
    imgMake(&color, W, eyeH, VK_FORMAT_R8G8B8A8_SRGB,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    imgMake(&depth, W, eyeH, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            VK_IMAGE_ASPECT_DEPTH_BIT);

    MlxrTarget t = {0};
    t.view = color.view;
    t.format = color.format;
    t.rect = (VkRect2D){{0, 0}, {W, eyeH}};
    t.clear = 1;
    for (int c = 0; c < 3; c++) t.clearColor[c] = srgbToLinear(room->clear[c]);
    t.clearColor[3] = 1.0f;

    MlxrRoomParams rp;
    memset(&rp, 0, sizeof(rp));
    mlxr_room_fill(room, &rp);
    rp.ambi = ambiImg.view;
    rp.eyes = eyes;
    RoomLevels levels;
    roomLevelsDefault(&levels);
    for (int e = 0; e < eyes; e++) {
        eyeView(e, room->farZ, rp.viewProj[e]);
        rp.eyeRect[e] = (VkRect2D){{(int32_t)(e * eyeW), 0}, {eyeW, eyeH}};
    }
    rp.spillGain = roomSpillGainFor(lit, room->style, room->spillGain, levels.lightLevel);
    rp.dim = roomDimFor(dimOverride, room->style, levels.brightness, room->dim);
    rp.depth = depth.view;
    rp.depthFormat = depth.format;

    VkCommandBuffer cmd = begin();
    mlxr_barrier(cmd, color.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    mlxr_barrier(cmd, depth.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    mlxr_pass_room(passes, cmd, &t, &rp);
    if (screenPicture) {
        // 16:9 picture at the placement the room gives it
        RoomLevels lv;
        roomLevelsDefault(&lv);
        RoomPlacement pl = roomPlacement(room->style, room->scale, 9.0f / 16.0f,
                                         roomScreenPercentOf(lv.screen[room->style], room->style));
        float model[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, pl.x, pl.y, pl.z, 1};
        MlxrPanelParams pp;
        memset(&pp, 0, sizeof(pp));
        pp.src = shotImg.view;
        pp.eyes = eyes;
        for (int e = 0; e < eyes; e++) {
            matMul(pp.mvp[e], rp.viewProj[e], model);
            pp.eyeRect[e] = rp.eyeRect[e];
            pp.uvRect[e][0] = 0;
            pp.uvRect[e][1] = 0;
            pp.uvRect[e][2] = 1;
            pp.uvRect[e][3] = 1;
        }
        pp.width = pl.width;
        pp.height = pl.height;
        pp.segments = 1;
        for (int c = 0; c < 4; c++) pp.color[c] = 1.0f;
        MlxrTarget tp = t;
        tp.clear = 0;
        colorBarrier(cmd, color.image);
        mlxr_pass_panel(passes, cmd, &tp, &pp);
    }
    mlxr_barrier(cmd, color.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    mlxr_barrier(cmd, depth.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    Host cb, db;
    hostMake(&cb, (uint64_t)W * eyeH * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    hostMake(&db, (uint64_t)W * eyeH * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    VkBufferImageCopy rg = {0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {W, eyeH, 1}};
    vkCmdCopyImageToBuffer(cmd, color.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, cb.buf, 1, &rg);
    rg.imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    vkCmdCopyImageToBuffer(cmd, depth.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, db.buf, 1, &rg);
    int rc = submit(cmd);
    f->eyeW = eyeW;
    f->eyeH = eyeH;
    f->eyes = eyes;
    f->rgba = malloc((size_t)W * eyeH * 4);
    f->depth = malloc((size_t)W * eyeH * 4);
    memcpy(f->rgba, cb.map, (size_t)W * eyeH * 4);
    memcpy(f->depth, db.map, (size_t)W * eyeH * 4);
    hostFree(&cb);
    hostFree(&db);
    imgFree(&color);
    imgFree(&depth);
    return rc;
}

static void projectPoint(const float* m, float x, float y, float z, uint32_t eyeW, uint32_t eyeH, double* px, double* py,
                         double* depth) {
    double c[4];
    for (int i = 0; i < 4; i++) c[i] = m[i] * x + m[4 + i] * y + m[8 + i] * z + m[12 + i];
    double nx = c[0] / c[3], ny = c[1] / c[3], nz = c[2] / c[3];
    *px = (nx + 1.0) * 0.5 * eyeW;
    *py = (1.0 - ny) * 0.5 * eyeH;
    *depth = 0.5 * (nz + 1.0);
}

// Eye space distance of a [0,1] depth value
static double linearDepth(double d, double n, double f) {
    double ndc = 2.0 * d - 1.0;
    return 2.0 * f * n / ((f + n) - ndc * (f - n));
}

typedef struct {
    double coverage;   // fraction of pixels with depth < 1
    double minDepth;
    double meanLuma;   // 0..255
    int nonFinite;     // NaN or out of [0,1] depths
    double leftRightDiff; // fraction of pixels differing between the eyes
} Stats;

static Stats frameStats(const Frame* f) {
    Stats s = {0};
    const uint32_t W = f->eyeW * f->eyes;
    const size_t n = (size_t)W * f->eyeH;
    size_t cov = 0;
    double luma = 0;
    s.minDepth = 1.0;
    for (size_t i = 0; i < n; i++) {
        float d = f->depth[i];
        if (!(d >= 0.0f && d <= 1.0f)) s.nonFinite++;
        else {
            if (d < 1.0f) cov++;
            if (d < s.minDepth) s.minDepth = d;
        }
        const uint8_t* p = f->rgba + i * 4;
        luma += 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
    }
    s.coverage = (double)cov / (double)n;
    s.meanLuma = luma / (double)n;
    if (f->eyes == 2) {
        size_t diff = 0;
        for (uint32_t y = 0; y < f->eyeH; y++)
            for (uint32_t x = 0; x < f->eyeW; x++) {
                const uint8_t* a = f->rgba + ((size_t)y * W + x) * 4;
                const uint8_t* b = f->rgba + ((size_t)y * W + f->eyeW + x) * 4;
                if (abs(a[0] - b[0]) + abs(a[1] - b[1]) + abs(a[2] - b[2]) > 6) diff++;
            }
        s.leftRightDiff = (double)diff / ((double)f->eyeW * f->eyeH);
    }
    return s;
}

// ------------------------------------------------------------- logic

static void testLogic(void) {
    // table and params
    check(ROOM_STYLE_LAST - ROOM_STYLE_FIRST + 1 == 3, "three rooms");
    check(roomStyleForCell(ENV_CELL_HOME_THEATER) == ROOM_STYLE_THEATER && roomStyleForCell(ENV_CELL_GRAND_CINEMA) == 2 &&
              roomStyleForCell(ENV_CELL_SYNTHWAVE) == 3 && roomStyleForCell(ENV_CELL_VOID) == 0 &&
              roomStyleForCell(ENV_CELL_PASSTHROUGH) == 0,
          "cell to style");
    for (int s = 1; s <= 3; s++) check(roomStyleForCell(roomCellForStyle(s)) == s, "style %d round trips", s);
    check(roomCellForStyle(0) == -1 && roomCellForStyle(9) == -1, "no cell for no room");
    check(roomEffectiveStyle(-1, 2) == 2 && roomEffectiveStyle(0, 2) == 0 && roomEffectiveStyle(3, 0) == 3,
          "override wins unless negative");
    check(bakedRoomStyle(1) && bakedRoomStyle(3) && !bakedRoomStyle(0) && !bakedRoomStyle(4), "baked styles");
    check(roomModel(0) == roomModel(1) && roomModel(99) == roomModel(1), "unknown style reads the first row");

    RoomParams t = roomParams(ROOM_STYLE_THEATER, 1.0f);
    check(NEAR(t.anchor.x, 0, 1e-6) && NEAR(t.anchor.y, 1.5, 1e-5) && NEAR(t.anchor.z, 1.02, 1e-5), "theater anchor");
    check(NEAR(t.screenZ, -4.152, 1e-4) && NEAR(t.screenMountY, 0.05, 1e-5), "theater screen z %g y %g", t.screenZ, t.screenMountY);
    check(NEAR(t.floorY, -1.64, 1e-5) && NEAR(t.ceilingY, 1.34, 1e-5) && NEAR(t.backZ, 3.39, 1e-4), "theater walls");
    check(NEAR(t.spillRadius, 3.0, 1e-6) && NEAR(t.spillGain, 0.55, 1e-6) && NEAR(t.dim, 0.37, 1e-6) && t.texMix == 1.0f &&
              !t.open,
          "theater light");
    RoomParams t2 = roomParams(ROOM_STYLE_THEATER, 2.0f);
    check(NEAR(t2.screenZ, 2 * t.screenZ, 1e-4) && NEAR(t2.screenWidth, 2 * t.screenWidth, 1e-5) &&
              NEAR(t2.floorY, 2 * t.floorY, 1e-5) && NEAR(t2.spillRadius, 6.0, 1e-6) && t2.spillGain == t.spillGain,
          "scale scales lengths only");
    RoomParams c = roomParams(ROOM_STYLE_GRAND_CINEMA, 1.0f);
    check(NEAR(c.anchor.y, 5.2, 1e-5) && NEAR(c.screenZ, -21.42, 1e-4) && NEAR(c.screenMountY, 2.2, 1e-5) && NEAR(c.dim, 0.21, 1e-6),
          "cinema params");
    RoomParams w = roomParams(ROOM_STYLE_SYNTHWAVE, 1.0f);
    check(w.open && NEAR(w.anchor.y, 1.55, 1e-5) && NEAR(w.screenMountY, 2.95, 1e-5) && NEAR(w.screenZ, -14.0 + 0.10, 1e-4) &&
              NEAR(w.ceilingY, 1800 - 1.55, 1e-3),
          "synthwave params");
    check(roomResizable(1) && !roomResizable(2) && roomResizable(3) && !roomResizable(0) && !roomResizable(7), "resizable");

    // levels
    RoomLevels lv;
    roomLevelsDefault(&lv);
    check(lv.brightness[1] == 37 && lv.brightness[2] == 21 && lv.brightness[3] == 53, "default brightness");
    check(lv.glow[1] == 1 && lv.glow[2] == 0 && lv.glow[3] == 1, "default glow");
    check(lv.lightLevel[1] == 150 && lv.lightLevel[2] == 150 && lv.lightLevel[3] == 150, "default light level");
    check(lv.screen[1] == 80 && lv.screen[2] == 100 && lv.screen[3] == 100, "default screen");
    roomLevelsSet(&lv, 1, 500, 7, -4, 10);
    check(lv.brightness[1] == ROOM_BRIGHTNESS_MAX && lv.glow[1] == 1 && lv.lightLevel[1] == ROOM_LIGHT_MIN &&
              lv.screen[1] == ROOM_SCREEN_MIN,
          "levels clamp");
    roomLevelsSet(&lv, 2, 1, 0, 999, 40);
    check(lv.brightness[2] == ROOM_BRIGHTNESS_MIN && lv.lightLevel[2] == ROOM_LIGHT_MAX && lv.screen[2] == 100,
          "a room that cannot be resized keeps 100");
    roomLevelsSet(&lv, 0, 1, 1, 1, 1);
    check(roomScreenPercentOf(60, 1) == 60 && roomScreenPercentOf(60, 2) == 100 && roomScreenPercentOf(5, 3) == 25 &&
              roomScreenPercentOf(5, 0) == ROOM_SCREEN_MAX && roomScreenPercentOf(150, 1) == 100,
          "screen percent");
    check(roomGlowOf(1, 0, 1) == 0 && roomGlowOf(1, 1, 0) == 1 && roomGlowOf(0, 0, 1) == 1 && roomGlowOf(0, 1, 0) == 0,
          "glow: the room's own in a room, the app's elsewhere");

    // scale, dim, spill gain, far plane
    check(roomScaleFor(0, 1) == 1.0f && roomScaleFor(2.5f, 1) == 2.5f && roomScaleFor(100.f, 2) == ROOM_SCALE_MAX &&
              roomScaleFor(0.01f, 2) == 0.01f * 0 + ROOM_SCALE_MIN && roomScaleFor(3.f, 0) == 1.0f,
          "room scale");
    int br[4] = {0, 37, 21, 53};
    int ll[4] = {0, 150, 100, 0};
    check(NEAR(roomDimFor(0, 2, br, 0.9f), 0.21, 1e-6) && NEAR(roomDimFor(0, 0, br, 0.9f), 0.9, 1e-6) &&
              NEAR(roomDimFor(5.f, 2, br, 0.9f), ROOM_DIM_MAX, 1e-6) && NEAR(roomDimFor(0.5f, 2, br, 0.9f), 0.5, 1e-6),
          "dim: override, row, fallback");
    check(NEAR(roomSpillGainFor(1, 1, 0.55f, ll), 0.825, 1e-6) && roomSpillGainFor(0, 1, 0.55f, ll) == 0.0f &&
              NEAR(roomSpillGainFor(1, 2, 0.55f, ll), 0.55, 1e-6) && roomSpillGainFor(1, 3, 0.4f, ll) == 0.0f &&
              NEAR(roomSpillGainFor(1, 0, 0.4f, ll), 0.4, 1e-6),
          "spill gain");
    check(roomFarPlane(&t, 4.0f) == ROOM_FAR_MIN_M, "a small room keeps the floor far plane");
    check(NEAR(roomFarPlane(&w, 10.0f), 1800.1 * 1.5, 1e-2), "the sky is inside the frustum (%g)", roomFarPlane(&w, 10.0f));
    check(NEAR(roomFarPlane(&c, 100.0f), 150.0, 1e-3), "far plane from the reach");

    // dither and spill weight
    double dmin = 1, dmax = -1, dsum = 0;
    for (unsigned i = 0; i < 100000; i++) {
        double d = roomDither(0xc2b2ae35u, i);
        if (d < dmin) dmin = d;
        if (d > dmax) dmax = d;
        dsum += d;
    }
    check(dmin >= -1.0 / 255 - 1e-6 && dmax <= 1.0 / 255 + 1e-6 && fabs(dsum / 100000) < 1e-4, "dither within one 255th, mean 0 (%g %g %g)", dmin,
          dmax, dsum / 100000);
    check(roomDither(1, 5) == roomDither(1, 5) && roomDither(1, 5) != roomDither(2, 5), "dither depends on the seed");
    Vec3 up = {0, 1, 0}, toward = {0, 0, -1}, away = {0, 0, 1};
    check(roomSpillWeight(&t, t.screenAt, up) == 1.0f, "weight 1 at the light");
    Vec3 pos = {0, t.screenMountY, t.screenZ + 3.0f};
    check(NEAR(roomSpillWeight(&t, pos, toward), 0.5, 1e-4), "half at the radius facing it (%g)", roomSpillWeight(&t, pos, toward));
    check(roomSpillWeight(&t, pos, away) == 0.0f, "no light on a face turned away");
    Vec3 behind = {0, t.screenMountY, 1.0f};
    Vec3 front = {0, t.screenMountY, -1.0f};
    float wb = roomSpillWeight(&t, behind, toward), wf = roomSpillWeight(&t, front, toward);
    check(wb < wf && wb >= 0.0f, "faded behind the viewer");
    Vec3 farBack = {0, 0, t.backZ + 1.0f};
    check(roomSpillWeight(&t, farBack, toward) == 0.0f, "none past the back wall");

    // vertex layout
    float src[ROOM_MODEL_FLOATS * 2] = {1.0f, 2.0f, 0.0f, 0, 0, 1, 0.25f, 0.75f, 0.1f, 0.2f, 0.3f,
                                        1.0f, 2.0f, 5.0f, 1, 0, 0, 0.5f, 0.5f, 0.9f, 0.8f, 0.7f};
    float out[ROOM_VERTEX_FLOATS * 2];
    float reach = roomBuildVertices(&t, 1.0f, src, 2, out);
    float ex = 1.0f - t.anchor.x, ey = 2.0f - t.anchor.y, ez = 5.0f - t.anchor.z;
    check(NEAR(out[0], 1.0, 1e-6) && NEAR(out[1], 0.5, 1e-6) && NEAR(out[2], -1.02, 1e-5), "vertex position is (model - anchor)");
    check(NEAR(out[7], 0.25, 1e-6) && NEAR(out[8], 0.75, 1e-6) && out[9] == 0.0f, "vertex uv and spare");
    check(NEAR(out[3], 0.1, 0.0045) && NEAR(out[4], 0.2, 0.0045) && NEAR(out[5], 0.3, 0.0045) && NEAR(out[3] - 0.1, out[4] - 0.2, 1e-6),
          "vertex colour plus one dither");
    check(NEAR(reach, sqrtf(ex * ex + ey * ey + ez * ez), 1e-4), "reach is the furthest vertex (%g)", reach);
    float out2[ROOM_VERTEX_FLOATS * 2];
    roomBuildVertices(&t, 2.0f, src, 2, out2);
    check(NEAR(out2[0], 2 * out[0], 1e-5) && NEAR(out2[1], 2 * out[1], 1e-5) && out2[7] == out[7], "scale applies to positions only");

    // where the picture hangs
    RoomPlacement p = roomPlacement(ROOM_STYLE_THEATER, 1.0f, 9.0f / 16.0f, 80);
    check(NEAR(p.width, 2.88, 1e-3) && NEAR(p.height, 1.62, 1e-3) && NEAR(p.x, 0, 1e-6) && NEAR(p.y, 0.05, 1e-4) &&
              NEAR(p.z, -4.152, 1e-3),
          "theater 16:9 at 80 percent: %.3f x %.3f at %.3f %.3f %.3f", p.width, p.height, p.x, p.y, p.z);
    p = roomPlacement(ROOM_STYLE_THEATER, 1.0f, 0.75f, 100);
    check(NEAR(p.height, 2.025, 1e-3) && NEAR(p.width, 2.7, 1e-3), "a taller picture loses width (%.3f x %.3f)", p.width, p.height);
    p = roomPlacement(ROOM_STYLE_THEATER, 1.0f, 9.0f / 21.0f, 100);
    check(NEAR(p.width, 3.6, 1e-3), "a wider picture keeps the anchor width (%.3f)", p.width);
    p = roomPlacement(ROOM_STYLE_GRAND_CINEMA, 1.0f, 9.0f / 16.0f, 100);
    check(NEAR(p.width, 22.0, 1e-3) && NEAR(p.height, 12.375, 1e-3) && NEAR(p.y, 2.2, 1e-4) && NEAR(p.z, -21.42, 1e-3),
          "cinema fills its screen: %.3f x %.3f at y %.3f z %.3f", p.width, p.height, p.y, p.z);
    p = roomPlacement(ROOM_STYLE_SYNTHWAVE, 1.0f, 9.0f / 16.0f, 100);
    check(NEAR(p.width, 14.0, 1e-3) && NEAR(p.y, 2.95, 1e-4) && NEAR(p.z, -13.9, 1e-3), "synthwave hangs as written: %.3f at y %.3f z %.3f", p.width,
          p.y, p.z);
    // the bottom edge stays off the ground in the open room
    check(p.y - p.height * 0.5f > roomParams(3, 1.0f).floorY, "synthwave picture clears the ground");
    p = roomPlacement(ROOM_STYLE_THEATER, 1.0f, 3.0f, 100);
    check(p.y - p.height * 0.5f >= roomParams(1, 1.0f).floorY && p.y + p.height * 0.5f <= roomParams(1, 1.0f).ceilingY,
          "a very tall picture is clamped between floor and ceiling");
    p = roomPlacement(ROOM_STYLE_THEATER, 0.5f, 9.0f / 16.0f, 80);
    check(NEAR(p.width, 1.44, 1e-3) && NEAR(p.z, -2.076, 1e-3), "placement follows the room scale");

    // eye size
    int ew, eh;
    roomEyeSize(ENV_RES_STANDARD, 1728, 1728, 0, 0, &ew, &eh);
    check(ew == 1728 && eh == 1728, "standard takes the Frame's recommendation");
    roomEyeSize(ENV_RES_STANDARD, 2200, 2000, 0, 0, &ew, &eh);
    check(ew == 1760 && eh == 1760, "standard is capped");
    roomEyeSize(ENV_RES_HIGH, 2200, 2000, 0, 0, &ew, &eh);
    check(ew == 2200 && eh == 2000, "high takes up to 2560");
    roomEyeSize(ENV_RES_LOW, 1728, 1728, 0, 0, &ew, &eh);
    check(ew == 864 && eh == 864, "low is half");
    roomEyeSize(ENV_RES_ULTRA, 1728, 1728, 8192, 2000, &ew, &eh);
    check(ew == 2800 && eh == 2000, "ultra ignores the recommendation, within the ceiling");
    roomEyeSize(ENV_RES_STANDARD, 0, 0, 0, 0, &ew, &eh);
    check(ew == 1024 && eh == 1024, "no recommendation: 1024");
    // native (lane eye-swapchains): an image per eye, so each eye is held
    // inside SteamVR's 3458 on its own, whatever the tier; the standard tier
    // is no longer brought down to 1728 for two eyes side by side
    check(STEAMVR_TEXTURE_MAX == 3458, "the limit is SteamVR's 1.6 x 2160 + 2");
    ew = 1760, eh = 1760;
    check(roomEyeFitTexture(&ew, &eh) == 0 && ew == 1760 && eh == 1760, "standard at 1760 per eye is left alone");
    ew = 2800, eh = 2800;
    check(roomEyeFitTexture(&ew, &eh) == 0 && ew == 2800, "ultra at 2800 per eye fits");
    ew = 4000, eh = 3000;
    check(roomEyeFitTexture(&ew, &eh) == 1 && ew == 3458 && eh == 2594, "a wide eye is brought inside, aspect kept");
    ew = 2000, eh = 4000;
    check(roomEyeFitTexture(&ew, &eh) == 1 && eh == 3458 && ew == 1729, "a tall eye too");
    // what each tier gives per eye at the Frame's 2160 recommendation
    int tiers[4][2] = { { ENV_RES_LOW, 1080 }, { ENV_RES_STANDARD, 1760 }, { ENV_RES_HIGH, 2160 },
                        { ENV_RES_ULTRA, 2800 } };
    for (int i = 0; i < 4; i++) {
        roomEyeSize(tiers[i][0], 2160, 2160, 0, 0, &ew, &eh);
        roomEyeFitTexture(&ew, &eh);
        check(ew == tiers[i][1] && eh == tiers[i][1] && ew <= STEAMVR_TEXTURE_MAX, "tier %d at 2160: %d per eye",
              tiers[i][0], ew);
    }
    // MLXR_EYE_SCALE and the picture's eye
    check(eyeScaleParse(NULL) == 1.0f && eyeScaleParse("") == 1.0f && eyeScaleParse("x") == 1.0f, "scale unset: 1");
    check(eyeScaleParse("1.25") == 1.25f && eyeScaleParse("0.1") == EYE_SCALE_MIN && eyeScaleParse("9") == EYE_SCALE_MAX,
          "scale clamped to 0.5..1.6");
    check(pictureEyeSize(2160, 2160, 0, 0, 1.0f, &ew, &eh) == 1 && ew == 2160 && eh == 2160, "2160 per eye fits");
    check(pictureEyeSize(2160, 2160, 0, 0, 1.6f, &ew, &eh) == 1 && ew == 3456, "1.6 x 2160 = 3456 still fits");
    check(pictureEyeSize(3408, 3408, 0, 0, 1.6f, &ew, &eh) == 0 && ew == 5453, "past 3458 is said, not capped");
    check(pictureEyeSize(1728, 1728, 0, 0, 1.25f, &ew, &eh) == 1 && ew == 2160, "headless 1728 x 1.25 = 2160");
    check(pictureEyeSize(2160, 2160, 3000, 2500, 1.6f, &ew, &eh) == 1 && ew == 3000 && eh == 2500,
          "within the runtime's maximum");
    check(pictureEyeSize(0, 0, 0, 0, 1.0f, &ew, &eh) == 1 && ew == 1728, "no recommendation: 1728");

    // view projection: a point straight ahead lands in the middle
    float vp[16];
    XrFovf fov = {-0.8f, 0.8f, 0.8f, -0.8f};
    XrPosef pose = {{0, 0, 0, 1}, {0, 0, 0}};
    roomEyeViewProj(vp, fov, pose, 60.0f);
    double px, py, d;
    projectPoint(vp, 0, 0, -3, 100, 100, &px, &py, &d);
    check(NEAR(px, 50, 1e-3) && NEAR(py, 50, 1e-3) && d > 0 && d < 1, "view projection centres a point ahead");
    projectPoint(vp, 0, 1, -3, 100, 100, &px, &py, &d);
    check(py < 50, "up is the top rows");
    check(NEAR(linearDepth(d, ROOM_NEAR_M, 60.0), sqrt(1.0) * 3.0, 1e-3), "linear depth (%g)", linearDepth(d, ROOM_NEAR_M, 60.0));
}

// ------------------------------------------------------------- load

static const char* styleName(int s) {
    return s == 1 ? "home_theater" : s == 2 ? "grand_cinema" : "synthwave";
}

static void* readBuffer(VkBuffer b, uint64_t size) {
    Host h;
    hostMake(&h, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    VkCommandBuffer cmd = begin();
    VkBufferCopy cp = {0, 0, size};
    vkCmdCopyBuffer(cmd, b, h.buf, 1, &cp);
    submit(cmd);
    void* out = malloc(size);
    memcpy(out, h.map, size);
    hostFree(&h);
    return out;
}

static unsigned char* readAll(const char* path, size_t* n) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *n = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char* d = malloc(*n);
    if (fread(d, 1, *n, f) != *n) {
        free(d);
        d = NULL;
    }
    fclose(f);
    return d;
}

static MlxrRoom* loadRoom(int style, int low, float scale) {
    MlxrRoomLoadInfo li = {MLXR_ASSETS_DIR, style, scale, low};
    MlxrRoom* r = NULL;
    int rc = mlxr_room_load(&gpu, &li, &r);
    check(rc == 0 && r, "load %s%s", styleName(style), low ? " (low)" : "");
    return rc == 0 ? r : NULL;
}

static void testLoadOne(int style) {
    const char* name = styleName(style);
    MlxrRoom* r = loadRoom(style, 0, 0.0f);
    if (!r) return;
    // The file, to check the upload against
    char path[512];
    snprintf(path, sizeof(path), "%s/rooms/%s.room", MLXR_ASSETS_DIR, name);
    size_t n;
    unsigned char* file = readAll(path, &n);
    RoomMeshInfo mi;
    check(file && roomMeshParse(file, n, &mi), "parse %s", path);
    if (!file) return;
    check(r->vertexCount == mi.vertexCount && r->indexCount == mi.indexCount && r->partCount == mi.partCount &&
              r->atlasCount == mi.atlasCount,
          "counts match the file");
    // Parts: exactly the file's, textured ones carry their atlas and the mix
    uint32_t covered = 0;
    for (uint32_t i = 0; i < r->partCount; i++) {
        check(r->parts[i].firstIndex == mi.parts[i].firstIndex && r->parts[i].indexCount == mi.parts[i].indexCount, "part %u range", i);
        int textured = mi.parts[i].atlas >= 0;
        check(textured ? (r->parts[i].atlas == r->atlas[mi.parts[i].atlas].view && r->parts[i].atlas != VK_NULL_HANDLE &&
                          r->parts[i].texMix == 1.0f)
                       : (r->parts[i].atlas == VK_NULL_HANDLE && r->parts[i].texMix == 0.0f),
              "part %u atlas and texMix", i);
        covered += r->parts[i].indexCount;
    }
    check(covered == r->indexCount, "parts cover every index");
    // Atlases: full size ASTC with its whole chain
    for (uint32_t i = 0; i < r->atlasCount; i++) {
        char ap[512];
        snprintf(ap, sizeof(ap), "%s/rooms/%s_%u.atlas", MLXR_ASSETS_DIR, name, i);
        size_t an;
        unsigned char* ad = readAll(ap, &an);
        AtlasInfo ai;
        check(ad && atlasParse(ad, an, &ai) > 0, "parse %s", ap);
        if (ad) {
            const MlxrRoomAtlas* a = &r->atlas[i];
            check(!a->low && a->width == ai.width && a->height == ai.height && (int)a->levels == ai.levels && a->width == 4096,
                  "atlas %u is the full size one, %ux%u, %u levels", i, a->width, a->height, a->levels);
            check(a->levels >= 2 && atlasLevelSize(a->width, (int)a->levels - 1) == 1, "atlas %u has its chain down to 1x1", i);
            check(a->format == VK_FORMAT_ASTC_6x6_UNORM_BLOCK, "atlas %u format 6x6 UNORM (%d)", i, a->format);
            check(a->gpuBytes >= an - 4096 && a->gpuBytes < 2 * an, "atlas %u memory %llu for %zu file bytes", i, (unsigned long long)a->gpuBytes, an);
            free(ad);
        }
    }
    // Vertex buffer read back equals the CPU build; all finite
    float* gpuV = readBuffer(r->vertices, r->vertexBytes);
    float* cpuV = malloc(r->vertexBytes);
    float reach = roomBuildVertices(&r->params, r->scale, (const float*)(file + mi.vertexOffset), (int)mi.vertexCount, cpuV);
    check(memcmp(gpuV, cpuV, r->vertexBytes) == 0, "vertex buffer on the GPU equals the build");
    size_t bad = 0;
    for (size_t i = 0; i < r->vertexBytes / 4; i++)
        if (!isfinite(gpuV[i])) bad++;
    check(bad == 0, "no NaN or inf in %zu vertex floats", r->vertexBytes / 4);
    uint32_t* gpuI = readBuffer(r->indices, r->indexBytes);
    check(memcmp(gpuI, file + mi.indexOffset, r->indexBytes) == 0, "index buffer on the GPU equals the file");
    uint32_t maxIdx = 0;
    for (uint32_t i = 0; i < r->indexCount; i++)
        if (gpuI[i] > maxIdx) maxIdx = gpuI[i];
    check(maxIdx < r->vertexCount, "indices inside the vertices");
    check(NEAR(r->reach, reach, 1e-3) && r->farZ >= ROOM_FAR_MIN_M && r->farZ >= r->reach, "far plane %.1f for reach %.1f", r->farZ, r->reach);
    free(gpuV);
    free(cpuV);
    free(gpuI);
    free(file);
    note("%-13s load %7.1f ms: %u vertices, %u indices, %u parts, %u atlas; vertex %.2f MB, index %.2f MB, atlas %.2f MB (file %.2f MB), "
         "total %.2f MB, far %.0f m",
         name, r->loadMs, r->vertexCount, r->indexCount, r->partCount, r->atlasCount, r->vertexBytes / 1048576.0,
         r->indexBytes / 1048576.0, (r->gpuBytes - r->vertexBytes - r->indexBytes) / 1048576.0,
         (r->atlasCount ? r->atlas[0].fileBytes : 0) / 1048576.0, r->gpuBytes / 1048576.0, r->farZ);
    // Unload and load again: the second load is warm in the file cache
    mlxr_room_unload(&gpu, r);
    MlxrRoom* r2 = loadRoom(style, 0, 0.0f);
    if (r2) {
        note("%-13s reload %6.1f ms", name, r2->loadMs);
        mlxr_room_unload(&gpu, r2);
    }
    // The low atlas
    MlxrRoom* lo = loadRoom(style, 1, 0.0f);
    if (lo) {
        check(lo->atlasCount == 1 && lo->atlas[0].low && lo->atlas[0].width == 2048 && lo->atlas[0].levels == 12,
              "the _lo_ atlas when asked: %ux%u, %u levels", lo->atlas[0].width, lo->atlas[0].height, lo->atlas[0].levels);
        note("%-13s low atlas load %5.1f ms, total %.2f MB", name, lo->loadMs, lo->gpuBytes / 1048576.0);
        mlxr_room_unload(&gpu, lo);
    }
}

static void testLoad(void) {
    for (int s = 1; s <= 3; s++) testLoadOne(s);
    // Not found / wrong style
    MlxrRoom* r = NULL;
    MlxrRoomLoadInfo bad = {"/nonexistent", 1, 0, 0};
    check(mlxr_room_load(&gpu, &bad, &r) != 0 && r == NULL, "a missing assets dir is an error");
    MlxrRoomLoadInfo bad2 = {MLXR_ASSETS_DIR, 0, 0, 0};
    check(mlxr_room_load(&gpu, &bad2, &r) != 0 && r == NULL, "style 0 is no room");
    // The controller model
    MlxrModel* m = NULL;
    check(mlxr_model_load(&gpu, MLXR_ASSETS_DIR, &m) == 0 && m, "load the controller model");
    if (m) {
        char path[512];
        snprintf(path, sizeof(path), "%s/models/controller.room", MLXR_ASSETS_DIR);
        size_t n;
        unsigned char* file = readAll(path, &n);
        RoomMeshInfo mi;
        check(file && roomMeshParse(file, n, &mi), "parse the model");
        check(m->vertexCount == mi.vertexCount && m->indexCount == mi.indexCount, "model counts");
        void* gv = readBuffer(m->vertices, m->vertexBytes);
        void* gi = readBuffer(m->indices, m->indexBytes);
        check(memcmp(gv, file + mi.vertexOffset, m->vertexBytes) == 0 && memcmp(gi, file + mi.indexOffset, m->indexBytes) == 0,
              "model buffers equal the file");
        note("controller    load %7.1f ms: %u vertices, %u triangles, %.3f MB on the GPU", m->loadMs, m->vertexCount, m->indexCount / 3,
             m->gpuBytes / 1048576.0);
        free(gv);
        free(gi);
        free(file);
        mlxr_model_unload(&gpu, m);
    }
}

static void testRescale(void) {
    MlxrRoom* r = loadRoom(ROOM_STYLE_THEATER, 0, 0.0f);
    if (!r) return;
    check(r->scale == 1.0f, "built at scale 1");
    float farBefore = r->farZ;
    VkCommandBuffer cmd = begin();
    check(mlxr_room_rescale(&gpu, r, 2.0f, cmd) == 0, "rescale records");
    submit(cmd);
    mlxr_room_upload_done(&gpu, r);
    char path[512];
    snprintf(path, sizeof(path), "%s/rooms/home_theater.room", MLXR_ASSETS_DIR);
    size_t n;
    unsigned char* file = readAll(path, &n);
    RoomMeshInfo mi;
    roomMeshParse(file, n, &mi);
    RoomParams p2 = roomParams(1, 2.0f);
    float* want = malloc(r->vertexBytes);
    roomBuildVertices(&p2, 2.0f, (const float*)(file + mi.vertexOffset), (int)mi.vertexCount, want);
    float* got = readBuffer(r->vertices, r->vertexBytes);
    check(r->scale == 2.0f && memcmp(want, got, r->vertexBytes) == 0, "the vertex buffer is rebuilt at scale 2");
    check(r->farZ >= farBefore && NEAR(r->params.screenZ, -8.304, 1e-3), "params follow the scale");
    free(want);
    free(got);
    free(file);
    mlxr_room_unload(&gpu, r);
}

// ------------------------------------------------------------- render

static void saveFrame(const char* name, const Frame* f) {
    writePng(name, f->rgba, f->eyeW * f->eyes, f->eyeH);
}

static void testRenderOne(int style) {
    const char* name = styleName(style);
    MlxrRoom* r = loadRoom(style, 0, 0.0f);
    if (!r) return;
    const uint32_t EW = 1280, EH = 1280;
    Frame f;
    double t0 = nowMs();
    check(renderRoom(r, EW, EH, 2, 1, 1, &f) == 0, "render %s", name);
    double ms = nowMs() - t0;
    Stats s = frameStats(&f);
    char img[128];
    snprintf(img, sizeof(img), "room_%s", name);
    saveFrame(img, &f);
    note("%-13s render %ux%u x2 incl. pipeline build %.0f ms: depth coverage %.3f, nearest depth %.5f, mean luma %.1f, eyes differ %.3f", name, EW, EH, ms, s.coverage,
         s.minDepth, s.meanLuma, s.leftRightDiff);
    check(s.nonFinite == 0, "%s: every depth value is a number in [0,1] (%d bad)", name, s.nonFinite);
    check(s.coverage > 0.95, "%s: the room covers the view (%.3f)", name, s.coverage);
    check(s.minDepth < 0.99, "%s: something is near (%.5f)", name, s.minDepth);
    check(s.meanLuma > 6.0 && s.meanLuma < 230.0, "%s: sensible brightness %.1f", name, s.meanLuma);
    check(s.leftRightDiff > 0.01, "%s: the eyes differ (%.3f)", name, s.leftRightDiff);

    // The picture hangs on the wall: the wall behind its centre is where the picture is
    RoomPlacement pl = roomPlacement(style, r->scale, 9.0f / 16.0f, roomScreenPercentOf(roomModel(style)->screenFraction * 100.0f + 0.5f, style));
    for (int eye = 0; eye < 2; eye++) {
        float vp[16];
        eyeView(eye, r->farZ, vp);
        double px, py, d;
        projectPoint(vp, pl.x, pl.y, pl.z, EW, EH, &px, &py, &d);
        int ix = (int)px, iy = (int)py;
        check(ix >= 0 && ix < (int)EW && iy >= 0 && iy < (int)EH, "%s eye %d: the picture centre is in view (%.0f, %.0f)", name, eye, px, py);
        if (ix < 0 || ix >= (int)EW || iy < 0 || iy >= (int)EH) continue;
        float wall = f.depth[(size_t)iy * (EW * 2) + eye * EW + ix];
        double dWall = linearDepth(wall, ROOM_NEAR_M, r->farZ), dPic = linearDepth(d, ROOM_NEAR_M, r->farZ);
        note("%-13s eye %d: picture centre at %.0f,%.0f, distance %.3f m, the surface there is at %.3f m", name, eye, px, py, dPic, dWall);
        check(wall < 1.0f, "%s eye %d: geometry behind the picture", name, eye);
        // top left bar of the test picture is white, picked a little inside
        double lx = pl.x - pl.width * 0.5 + pl.width * 0.03, ly = pl.y + pl.height * 0.5 - pl.height * 0.1;
        projectPoint(vp, (float)lx, (float)ly, pl.z, EW, EH, &px, &py, &d);
        const uint8_t* q = f.rgba + ((size_t)(int)py * EW * 2 + eye * EW + (int)px) * 4;
        check(q[0] > 200 && q[1] > 200 && q[2] > 200, "%s eye %d: the white bar is at the top left of the picture (%d %d %d)", name, eye, q[0], q[1], q[2]);
        // and the bottom edge of the picture is above the floor, inside the view
        projectPoint(vp, pl.x, pl.y - pl.height * 0.5f, pl.z, EW, EH, &px, &py, &d);
        check(py > 0 && py < EH, "%s eye %d: the picture's bottom edge is in view (row %.0f)", name, eye, py);
    }
    // Unlit: the wash is gone and the room is no brighter than lit
    Frame fu;
    check(renderRoom(r, 320, 320, 2, 0, 0, &fu) == 0, "render %s unlit", name);
    Frame fl;
    check(renderRoom(r, 320, 320, 2, 0, 1, &fl) == 0, "render %s lit", name);
    Stats su = frameStats(&fu), sl = frameStats(&fl);
    note("%-13s mean luma unlit %.2f, lit by a bright blue-white sample %.2f", name, su.meanLuma, sl.meanLuma);
    check(sl.meanLuma >= su.meanLuma, "%s: the picture's light only adds", name);
    check(sl.meanLuma > su.meanLuma + 0.05, "%s: and it does show (%.2f -> %.2f)", name, su.meanLuma, sl.meanLuma);
    snprintf(img, sizeof(img), "room_%s_unlit", name);
    saveFrame(img, &fu);
    frameFree(&fu);
    frameFree(&fl);

    // Mips: a small render samples the lower levels. Compare it with a box
    // filtered large render; garbage or missing levels would be far apart.
    Frame big, small;
    dimOverride = 1.0f; // at the row's dim the differences would hide in the dark
    check(renderRoom(r, 1024, 1024, 1, 0, 0, &big) == 0 && renderRoom(r, 128, 128, 1, 0, 0, &small) == 0, "render mip pair");
    double sum = 0;
    for (int y = 0; y < 128; y++)
        for (int x = 0; x < 128; x++) {
            double acc[3] = {0, 0, 0};
            for (int dy = 0; dy < 8; dy++)
                for (int dx = 0; dx < 8; dx++) {
                    const uint8_t* b = big.rgba + ((size_t)(y * 8 + dy) * 1024 + x * 8 + dx) * 4;
                    for (int c = 0; c < 3; c++) acc[c] += srgbToLinear(b[c] / 255.0f);
                }
            const uint8_t* s2 = small.rgba + ((size_t)y * 128 + x) * 4;
            for (int c = 0; c < 3; c++) sum += fabs(srgbToLinear(s2[c] / 255.0f) - acc[c] / 64.0);
        }
    double mad = sum / (128.0 * 128 * 3) * 255.0;
    note("%-13s 1024 -> 128 mip consistency: mean abs difference %.2f / 255 (linear)", name, mad);
    check(mad < 4.0, "%s: the small render matches the filtered large one (%.2f)", name, mad);
    dimOverride = 0.0f;
    frameFree(&big);
    frameFree(&small);
    frameFree(&f);
    mlxr_room_unload(&gpu, r);
}

// ------------------------------------------------------------- model

// Native: the grips, which hands show and the textured pass's debug view,
// for renderModelsAt; renderModels holds both hands up in front
typedef struct {
    XrPosef grip[2];
    int shown[2];
    int debugView;
    float gain;
    // Native: the controls the textured models' moving parts follow (animate 1)
    int animate;
    MlxrModelInput input[2];
} ModelPose;

static int renderModelsAt(const MlxrModel* m, uint32_t eyeW, uint32_t eyeH, const ModelPose* pose, Frame* f);

static int renderModels(const MlxrModel* m, uint32_t eyeW, uint32_t eyeH, Frame* f) {
    // Both hands held up in front, pointing forward and down a little, the
    // left a little closer so they overlap in depth from one eye
    Vec3 ax = {1, 0, 0};
    XrQuaternionf tilt = axisAngleQuat(ax, -0.5f);
    ModelPose pose = {{{tilt, {-0.18f, -0.22f, -0.45f}}, {tilt, {0.20f, -0.20f, -0.50f}}}, {1, 1}, 0, 0.0f};
    return renderModelsAt(m, eyeW, eyeH, &pose, f);
}

static int renderModelsAt(const MlxrModel* m, uint32_t eyeW, uint32_t eyeH, const ModelPose* pose, Frame* f) {
    makeShared();
    memset(f, 0, sizeof(*f));
    const uint32_t W = eyeW * 2;
    Img color, depth;
    imgMake(&color, W, eyeH, VK_FORMAT_R8G8B8A8_SRGB,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    imgMake(&depth, W, eyeH, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            VK_IMAGE_ASPECT_DEPTH_BIT);
    MlxrTarget t = {0};
    t.view = color.view;
    t.format = color.format;
    t.rect = (VkRect2D){{0, 0}, {W, eyeH}};
    t.clear = 1;
    MlxrModelParams mp;
    memset(&mp, 0, sizeof(mp));
    mlxr_model_fill(m, &mp);
    mp.eyes = 2;
    for (int e = 0; e < 2; e++) {
        eyeView(e, ROOM_FAR_MIN_M, mp.viewProj[e]);
        mp.eyeRect[e] = (VkRect2D){{(int32_t)(e * eyeW), 0}, {eyeW, eyeH}};
    }
    // Native: only the generic model is mirrored for the left hand
    controllerModelMatrix(pose->grip[0], !m->perHand, mp.model[0]);
    controllerModelMatrix(pose->grip[1], 0, mp.model[1]);
    mp.shown[0] = pose->shown[0];
    mp.shown[1] = pose->shown[1];
    MlxrModelPart parts[2][MLXR_MODEL_COMPONENTS_MAX];
    if (pose->animate && m->textured) {
        for (int h = 0; h < 2; h++) {
            mp.handPartCount[h] = mlxr_model_pose(m, h, &pose->input[h], parts[h], MLXR_MODEL_COMPONENTS_MAX);
            mp.handParts[h] = parts[h];
        }
    }
    mp.debugView = pose->debugView;
    mp.gain = pose->gain;
    mp.depth = depth.view;
    mp.depthFormat = depth.format;

    VkCommandBuffer cmd = begin();
    mlxr_barrier(cmd, color.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    mlxr_barrier(cmd, depth.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    mlxr_pass_model(passes, cmd, &t, &mp);
    mlxr_barrier(cmd, color.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    mlxr_barrier(cmd, depth.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    Host cb, db;
    hostMake(&cb, (uint64_t)W * eyeH * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    hostMake(&db, (uint64_t)W * eyeH * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    VkBufferImageCopy rg = {0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {W, eyeH, 1}};
    vkCmdCopyImageToBuffer(cmd, color.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, cb.buf, 1, &rg);
    rg.imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    vkCmdCopyImageToBuffer(cmd, depth.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, db.buf, 1, &rg);
    int rc = submit(cmd);
    f->eyeW = eyeW;
    f->eyeH = eyeH;
    f->eyes = 2;
    f->rgba = malloc((size_t)W * eyeH * 4);
    f->depth = malloc((size_t)W * eyeH * 4);
    memcpy(f->rgba, cb.map, (size_t)W * eyeH * 4);
    memcpy(f->depth, db.map, (size_t)W * eyeH * 4);
    hostFree(&cb);
    hostFree(&db);
    imgFree(&color);
    imgFree(&depth);
    return rc;
}

// Premultiplied over a backdrop, for looking at
static void compositeOver(const uint8_t* top, const uint8_t* under, uint8_t* out, size_t n) {
    for (size_t i = 0; i < n; i++) {
        const uint8_t* t = top + i * 4;
        const uint8_t* u = under ? under + i * 4 : NULL;
        for (int c = 0; c < 3; c++) {
            int back = u ? u[c] : 40 + (int)((i * 7) % 3);
            int v = t[c] + back * (255 - t[3]) / 255;
            out[i * 4 + c] = (uint8_t)(v > 255 ? 255 : v);
        }
        out[i * 4 + 3] = 255;
    }
}

static void testModel(void) {
    MlxrModel* m = NULL;
    check(mlxr_model_load(&gpu, MLXR_ASSETS_DIR, &m) == 0, "load the controller model");
    if (!m) return;
    const uint32_t EW = 640, EH = 640;
    Frame f;
    check(renderModels(m, EW, EH, &f) == 0, "render the controller models");
    const size_t n = (size_t)EW * 2 * EH;
    size_t opaque = 0, partial = 0, depthCov = 0, corner = 0, depthMismatch = 0;
    int nonFinite = 0;
    for (size_t i = 0; i < n; i++) {
        uint8_t a = f.rgba[i * 4 + 3];
        float d = f.depth[i];
        if (a == 255) opaque++;
        else if (a != 0) partial++;
        if (!(d >= 0.0f && d <= 1.0f)) nonFinite++;
        if (d < 1.0f) depthCov++;
        if ((a == 255) != (d < 1.0f)) depthMismatch++;
        if (a == 0 && (f.rgba[i * 4] | f.rgba[i * 4 + 1] | f.rgba[i * 4 + 2])) corner++;
    }
    double cov = (double)opaque / (double)n;
    note("controller    render %ux%u x2: alpha coverage %.4f, partial %zu, depth coverage %.4f, depth/alpha mismatches %zu", EW, EH, cov, partial,
         (double)depthCov / n, depthMismatch);
    check(nonFinite == 0, "model depth finite");
    check(cov > 0.003 && cov < 0.5, "the models are on screen and not everywhere (%.4f)", cov);
    check(partial == 0, "alpha is 0 or 1 (%zu partial)", partial);
    check(corner == 0, "nothing but the models is drawn (%zu stray pixels)", corner);
    check(depthMismatch * 1000 < n, "depth is written exactly where the model is (%zu)", depthMismatch);
    check(f.rgba[3] == 0 && f.rgba[(n - 1) * 4 + 3] == 0, "the corners clear to nothing");
    // The model is lit: more than one shade on it
    int minL = 255, maxL = 0;
    for (size_t i = 0; i < n; i++)
        if (f.rgba[i * 4 + 3] == 255) {
            int l = (f.rgba[i * 4] + f.rgba[i * 4 + 1] + f.rgba[i * 4 + 2]) / 3;
            if (l < minL) minL = l;
            if (l > maxL) maxL = l;
        }
    check(maxL > minL + 20, "the model is shaded (%d..%d)", minL, maxL);
    // Mirrored: the left hand is the right hand's reflection, so the left
    // half of the left eye holds as much model as the right half of the right
    size_t l = 0, r = 0;
    for (uint32_t y = 0; y < EH; y++)
        for (uint32_t x = 0; x < EW; x++) {
            if (f.rgba[((size_t)y * EW * 2 + x) * 4 + 3] == 255) {
                if (x < EW / 2) l++;
                else r++;
            }
        }
    note("controller    left eye: %zu model pixels in the left half, %zu in the right", l, r);
    check(l > 0 && r > 0, "both hands show in the eye");
    uint8_t* comp = malloc(n * 4);
    compositeOver(f.rgba, NULL, comp, n);
    writePng("model_hands", comp, EW * 2, EH);
    free(comp);
    frameFree(&f);
    mlxr_model_unload(&gpu, m);
}

// ------------------------------------------------------------- textured model (native)
//
// The Steam Frame's textured pair (tools/frame-models, MXFM) through
// rooms/room_gpu.c and MLXR_PASS_MODEL_TEX: the parse rules, a made-up pair
// loaded and read back from the GPU, its mips, a render showing where each
// texel lands and that the colour is decoded once, the fallbacks, and (when
// the ctest before it could convert them from this machine's SteamVR) the
// real models rendered for looking at.

#define TQ_TEX 8
static const char* frameModelsDir; // argv[3]: converted real models, or none
// The made-up texture's quadrants, sRGB, from its top row: top left, top
// right, bottom left, bottom right
static const uint8_t kQuad[4][3] = {{200, 30, 30}, {30, 200, 30}, {30, 30, 200}, {230, 230, 230}};

// A made-up MXFM file: a 10 cm square facing +z in grip space, moved shiftX
// along x, as two components of one triangle each, an 8 x 8 texture of four
// quadrants (occlusion 1) and a flat normal map. truncate leaves the last
// byte off.
static void writeQuadModel(const char* path, float shiftX, int truncate, uint32_t version) {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    uint32_t head[8] = {0x4d46584d /* 'MXFM' */, version, 4, 6, 2, TQ_TEX, TQ_TEX, 0};
    fwrite(head, 4, 8, f);
    for (uint32_t c = 0; c < 2; c++) {
        unsigned char rec[208];
        memset(rec, 0, sizeof(rec));
        snprintf((char*)rec, 32, "%s", c ? "trigger" : "body");
        if (c) snprintf((char*)rec + 32, 32, "/input/trigger");
        uint32_t u[4] = {c * 3u, 3, c ? 2u : 0u, 0};
        memcpy(rec + 64, u, sizeof(u));
        float nan = NAN;
        memcpy(rec + 80 + MLXR_MOTION_PRESSED * 4, &nan, 4);
        fwrite(rec, 1, sizeof(rec), f);
    }
    const float P[4][2] = {{-0.05f, -0.05f}, {0.05f, -0.05f}, {0.05f, 0.05f}, {-0.05f, 0.05f}};
    const float UV[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}}; // v from the top row down
    for (int i = 0; i < 4; i++) {
        // normal +z, tangent +x (+u), w -1: the bitangent w cross(N, T) = -y runs along +v
        float v[12] = {P[i][0] + shiftX, P[i][1], 0, 0, 0, 1, 1, 0, 0, -1, UV[i][0], UV[i][1]};
        fwrite(v, 4, 12, f);
    }
    uint32_t idx[6] = {0, 1, 2, 0, 2, 3};
    fwrite(idx, 4, 6, f);
    uint8_t tex[2][TQ_TEX * TQ_TEX * 4];
    for (int y = 0; y < TQ_TEX; y++)
        for (int x = 0; x < TQ_TEX; x++) {
            int q = (y >= TQ_TEX / 2) * 2 + (x >= TQ_TEX / 2);
            uint8_t* c = tex[0] + (y * TQ_TEX + x) * 4;
            uint8_t* n = tex[1] + (y * TQ_TEX + x) * 4;
            c[0] = kQuad[q][0], c[1] = kQuad[q][1], c[2] = kQuad[q][2], c[3] = 255;
            n[0] = 128, n[1] = 128, n[2] = 255, n[3] = 128;
        }
    fwrite(tex, 1, sizeof(tex) - (truncate ? 1 : 0), f);
    fclose(f);
}

// A painted .room of one triangle (the older converter's format, MXR3)
static void writeTriRoom(const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    uint32_t head[5] = {0x3352584d /* 'MXR3' */, 3, 3, 1, 0};
    fwrite(head, 4, 5, f);
    uint32_t part[3] = {0, 3, 0xffffffffu};
    fwrite(part, 4, 3, f);
    uint32_t channels = 0;
    fwrite(&channels, 4, 1, f);
    const float pos[3][2] = {{-0.05f, -0.05f}, {0.05f, -0.05f}, {0, 0.05f}};
    for (int i = 0; i < 3; i++) {
        float v[11] = {pos[i][0], pos[i][1], 0, 0, 0, 1, 0, 0, 0.5f, 0.5f, 0.5f};
        fwrite(v, 4, 11, f);
    }
    uint32_t idx[3] = {0, 1, 2};
    fwrite(idx, 4, 3, f);
    fclose(f);
}

static const char* const kTexDir = "/tmp/mlxr-rooms-textured-test";

// A scratch assets dir with the generic controller only
static void stageModels(void) {
    char cmd[1400];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s' && mkdir -p '%s/models' && cp '%s/models/controller.room' '%s/models/'", kTexDir,
             kTexDir, MLXR_ASSETS_DIR, kTexDir);
    check(system(cmd) == 0, "stage %s", kTexDir);
}

static void modelPath(char* out, size_t n, const char* name) {
    snprintf(out, n, "%s/models/%s", kTexDir, name);
}

static double srgbDecode(double c) {
    c /= 255.0;
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

static double srgbEncode(double c) {
    c = c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1.0 / 2.4) - 0.055;
    return c * 255.0;
}

// One texel of a mip level of a model texture (left SHADER_READ_ONLY)
static void readTexel(const MlxrRoomAtlas* a, uint32_t level, uint8_t out[4]) {
    Host h;
    hostMake(&h, 256, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    VkCommandBuffer cmd = begin();
    mlxr_barrier(cmd, a->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy rg = {0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1}, {0, 0, 0}, {1, 1, 1}};
    vkCmdCopyImageToBuffer(cmd, a->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, h.buf, 1, &rg);
    mlxr_barrier(cmd, a->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    submit(cmd);
    memcpy(out, h.map, 4);
    hostFree(&h);
}

static void testModelTextured(void) {
    char left[700], right[700];
    stageModels();
    modelPath(left, sizeof(left), "frame_controller_left.model");
    modelPath(right, sizeof(right), "frame_controller_right.model");
    writeQuadModel(left, 0.0f, 0, 1);
    writeQuadModel(right, 0.0f, 0, 1);

    // The parse rules
    size_t n = 0;
    unsigned char* d = readAll(left, &n);
    MlxrModelFileInfo fi;
    memset(&fi, 0, sizeof(fi));
    check(d && mlxr_model_file_parse(d, n, n, &fi), "the made-up model parses");
    check(fi.vertexCount == 4 && fi.indexCount == 6 && fi.componentCount == 2 && fi.texWidth == TQ_TEX &&
              fi.texHeight == TQ_TEX && fi.vertexOffset == 32 + 2 * 208 && fi.colourOffset == 32 + 2 * 208 + 4 * 48 + 6 * 4 &&
              fi.surfaceOffset == fi.colourOffset + TQ_TEX * TQ_TEX * 4 && fi.size == n,
          "the layout (%u vertices, %u indices, %u components, %ux%u)", fi.vertexCount, fi.indexCount, fi.componentCount,
          fi.texWidth, fi.texHeight);
    if (d) {
        check(!mlxr_model_file_parse(d, n, n - 1, &fi) && !mlxr_model_file_parse(d, n, n + 1, &fi), "sizes must add up");
        check(!mlxr_model_file_parse(d, 31, n, &fi), "a short head is refused");
        d[0] = 'X';
        check(!mlxr_model_file_parse(d, n, n, &fi), "a wrong magic is refused");
        d[0] = 'M';
        d[4] = 3;
        check(!mlxr_model_file_parse(d, n, n, &fi), "another version is refused");
        d[4] = 2;
        check(mlxr_model_file_parse(d, n, n, &fi) && fi.version == 2, "version 2 (the d-pad's motion) is read");
        d[4] = 1;
        d[12] = 7; // 7 indices: not whole triangles
        check(!mlxr_model_file_parse(d, n, n, &fi), "indices not in whole triangles are refused");
    }

    // Loaded: the textured pair, each hand its own range, components absolute
    MlxrModel* m = NULL;
    check(mlxr_model_load(&gpu, kTexDir, &m) == 0 && m, "load the made-up textured pair");
    if (!m) {
        free(d);
        return;
    }
    note("textured     made-up pair: %s, %.0f KB, %u levels", m->source, m->gpuBytes / 1024.0, m->texLevels);
    check(m->textured && m->perHand, "textured and per hand");
    check(m->vertexCount == 8 && m->indexCount == 12 && m->handFirstIndex[1] == 6 && m->handIndexCount[0] == 6 &&
              m->handIndexCount[1] == 6 && m->handVertexOffset[1] == 4,
          "per hand ranges");
    check(m->componentCount[0] == 2 && m->componentCount[1] == 2 && !strcmp(m->components[1][1].name, "trigger") &&
              !strcmp(m->components[1][1].path, "/input/trigger") && m->components[1][1].firstIndex == 9 &&
              m->components[1][1].indexCount == 3 && m->components[1][1].motionType == MLXR_MOTION_ROTATE &&
              isnan(m->components[1][1].motion[MLXR_MOTION_PRESSED]) && m->components[0][0].firstIndex == 0,
          "components: names, input paths, absolute ranges, motions");
    check(m->texLevels == 4 && m->texture[0][0].levels == 4 && m->texture[1][1].levels == 4, "a full mip chain (%u)",
          m->texLevels);
    check(m->texture[0][0].format == VK_FORMAT_R8G8B8A8_SRGB && m->texture[0][1].format == VK_FORMAT_R8G8B8A8_UNORM,
          "colour sRGB, surface UNORM");
    MlxrModelParams mp;
    memset(&mp, 0, sizeof(mp));
    mlxr_model_fill(m, &mp);
    check(mp.textured && mp.perHand && mp.handColour[1] == m->texture[1][0].view && mp.handSurface[0] == m->texture[0][1].view,
          "fill hands over the images");

    // Round trip: the GPU's buffers hold the files' vertices and indices
    if (d) {
        unsigned char* vb = readBuffer(m->vertices, m->vertexBytes);
        unsigned char* ib = readBuffer(m->indices, m->indexBytes);
        int same = 1;
        for (int h = 0; h < 2; h++) {
            same &= memcmp(vb + (size_t)h * 4 * 48, d + fi.vertexOffset, 4 * 48) == 0;
            same &= memcmp(ib + (size_t)h * 6 * 4, d + fi.indexOffset, 6 * 4) == 0;
        }
        check(same, "vertices and indices read back as the files have them");
        free(vb);
        free(ib);
    }
    free(d);

    // The 1 x 1 mip is the four quadrants averaged in linear light (an sRGB
    // image filtered as such, not its bytes averaged)
    uint8_t texel[4];
    readTexel(&m->texture[0][0], 3, texel);
    double want[3];
    for (int c = 0; c < 3; c++) {
        double sum = 0;
        for (int q = 0; q < 4; q++) sum += srgbDecode(kQuad[q][c]);
        want[c] = srgbEncode(sum / 4.0);
    }
    note("textured     1x1 mip %u %u %u %u, linear-light average %.0f %.0f %.0f", texel[0], texel[1], texel[2], texel[3],
         want[0], want[1], want[2]);
    check(NEAR(texel[0], want[0], 3) && NEAR(texel[1], want[1], 3) && NEAR(texel[2], want[2], 3) && texel[3] == 255,
          "mips filtered in linear light");
    readTexel(&m->texture[0][1], 3, texel);
    check(NEAR(texel[0], 128, 1) && NEAR(texel[2], 255, 1) && NEAR(texel[3], 128, 1), "surface mips as they are");

    // Rendered: base colour x occlusion (debug view 3), each square 0.4 m
    // ahead of the eyes, the left 6 cm to the left and the right 6 cm to the
    // right. Each quadrant shows where the texture's own quadrant is (v runs
    // from the top row) in the colour the texture holds (decoded once: twice
    // would darken sRGB 200 to about 150).
    const uint32_t EW = 320, EH = 320;
    ModelPose pose = {{{{0, 0, 0, 1}, {-0.06f, 0.0f, -0.4f}}, {{0, 0, 0, 1}, {0.06f, 0.0f, -0.4f}}}, {1, 1}, 3, 0.0f};
    Frame f;
    check(renderModelsAt(m, EW, EH, &pose, &f) == 0, "render the made-up pair");
    float vp[16];
    eyeView(0, ROOM_FAR_MIN_M, vp);
    const float qx[4] = {-0.025f, 0.025f, -0.025f, 0.025f}, qy[4] = {0.025f, 0.025f, -0.025f, -0.025f};
    for (int h = 0; h < 2; h++)
        for (int q = 0; q < 4; q++) {
            double px, py, pz;
            projectPoint(vp, pose.grip[h].position.x + qx[q], qy[q], -0.4f, EW, EH, &px, &py, &pz);
            const uint8_t* c = f.rgba + ((size_t)py * EW * 2 + (size_t)px) * 4;
            int ok = NEAR(c[0], kQuad[q][0], 4) && NEAR(c[1], kQuad[q][1], 4) && NEAR(c[2], kQuad[q][2], 4) && c[3] == 255;
            check(ok, "hand %d quadrant %d at (%.0f, %.0f): %u %u %u %u, want %u %u %u", h, q, px, py, c[0], c[1], c[2], c[3],
                  kQuad[q][0], kQuad[q][1], kQuad[q][2]);
        }
    size_t npx = (size_t)EW * 2 * EH;
    uint8_t* comp = malloc(npx * 4);
    compositeOver(f.rgba, NULL, comp, npx);
    writePng("model_textured_quads_base", comp, EW * 2, EH);
    frameFree(&f);

    // Lit: shaded, opaque where drawn, nothing else drawn, and brighter with
    // the gain
    pose.debugView = 0;
    check(renderModelsAt(m, EW, EH, &pose, &f) == 0, "render the made-up pair lit");
    size_t opaque = 0, partial = 0, stray = 0;
    double luma = 0;
    for (size_t i = 0; i < npx; i++) {
        uint8_t a = f.rgba[i * 4 + 3];
        if (a == 255) {
            opaque++;
            luma += f.rgba[i * 4] + f.rgba[i * 4 + 1] + f.rgba[i * 4 + 2];
        }
        else if (a) partial++;
        if (!a && (f.rgba[i * 4] | f.rgba[i * 4 + 1] | f.rgba[i * 4 + 2])) stray++;
    }
    luma = opaque ? luma / (3.0 * opaque) : 0;
    check(opaque > 1000 && partial == 0 && stray == 0, "lit: %zu drawn, %zu partial, %zu stray", opaque, partial, stray);
    compositeOver(f.rgba, NULL, comp, npx);
    writePng("model_textured_quads_lit", comp, EW * 2, EH);
    frameFree(&f);
    pose.gain = 0.25f;
    renderModelsAt(m, EW, EH, &pose, &f);
    double dim = 0;
    for (size_t i = 0; i < npx; i++)
        if (f.rgba[i * 4 + 3] == 255) dim += f.rgba[i * 4] + f.rgba[i * 4 + 1] + f.rgba[i * 4 + 2];
    dim = opaque ? dim / (3.0 * opaque) : 0;
    note("textured     lit mean %.1f, at gain 0.25 %.1f", luma, dim);
    check(dim < luma - 10, "the gain dims the base colour");
    frameFree(&f);
    free(comp);
    mlxr_model_unload(&gpu, m);

    // The real models, when the ctest before converted them here
    char rl[700], rr[700];
    if (frameModelsDir) {
        snprintf(rl, sizeof(rl), "%s/frame_controller_left.model", frameModelsDir);
        snprintf(rr, sizeof(rr), "%s/frame_controller_right.model", frameModelsDir);
    }
    if (!frameModelsDir || access(rl, R_OK) != 0 || access(rr, R_OK) != 0) {
        note("textured     no converted Steam Frame models here (no SteamVR?): the real ones were not rendered");
        return;
    }
    char cmd[2400];
    stageModels();
    snprintf(cmd, sizeof(cmd), "cp '%s' '%s' '%s/models/'", rl, rr, kTexDir);
    check(system(cmd) == 0, "stage the real models");
    double t0 = nowMs();
    check(mlxr_model_load(&gpu, kTexDir, &m) == 0 && m && m->textured, "load the real textured pair");
    if (!m) return;
    note("textured     real pair: %u + %u triangles, %u vertices, textures %ux%u (%u levels), %.1f MB on the GPU, "
         "record %.1f ms, with the upload %.1f ms",
         m->handIndexCount[0] / 3, m->handIndexCount[1] / 3, m->vertexCount, m->texWidth, m->texHeight, m->texLevels,
         m->gpuBytes / (1024.0 * 1024.0), m->loadMs, nowMs() - t0);
    check(m->handIndexCount[0] / 3 > 8000 && m->handIndexCount[1] / 3 > 8000, "full detail");
    const uint32_t BW = 1024, BH = 1024;
    npx = (size_t)BW * 2 * BH;
    comp = malloc(npx * 4);
    // Both hands as testModel holds them, lit and as base colour x occlusion
    check(renderModels(m, BW, BH, &f) == 0, "render the real pair");
    compositeOver(f.rgba, NULL, comp, npx);
    writePng("model_textured_frame", comp, BW * 2, BH);
    frameFree(&f);
    // A close look at each hand: grip 0.16 m ahead, turned so its face and
    // the grip's indent show (the grip's -z, the trigger end, away and to the
    // side; tilted toward the eye)
    Vec3 up = {0, 1, 0}, ax = {1, 0, 0};
    for (int view = 0; view < 2; view++) {
        ModelPose close;
        memset(&close, 0, sizeof(close));
        for (int h = 0; h < 2; h++) {
            XrQuaternionf yaw = axisAngleQuat(up, h ? -0.6f : 0.6f);
            close.grip[h].orientation = quatMul(yaw, axisAngleQuat(ax, 0.6f));
            close.grip[h].position = (XrVector3f){h ? 0.06f : -0.06f, -0.03f, -0.16f};
            close.shown[h] = 1;
        }
        close.debugView = view ? 3 : 0;
        check(renderModelsAt(m, BW, BH, &close, &f) == 0, "render the real pair close");
        compositeOver(f.rgba, NULL, comp, npx);
        writePng(view ? "model_textured_frame_close_base" : "model_textured_frame_close", comp, BW * 2, BH);
        frameFree(&f);
    }
    free(comp);
    mlxr_model_unload(&gpu, m);
}


// ---- Native: the moving parts. A made-up model whose two components move as
// SteamVR's joystick/trigger, d-pad/button do, checked in numbers through
// mlxr_model_pose; then the real models (their trigger, grip, stick, d-pad and
// buttons each move the way a hand moves them) and a render with the parts moved.

// Overwrites component c of a written MXFM: its input path, motion type and motion floats
static void patchMotion(const char* file, int c, const char* path, uint32_t type, const float* mo, int moN) {
    FILE* f = fopen(file, "r+b");
    if (!f) return;
    unsigned char rec[208];
    long o = 32 + 208 * c;
    fseek(f, o, SEEK_SET);
    if (fread(rec, 1, sizeof(rec), f) != sizeof(rec)) {
        fclose(f);
        return;
    }
    memset(rec + 32, 0, 32);
    snprintf((char*)rec + 32, 32, "%s", path);
    memcpy(rec + 72, &type, 4);
    memset(rec + 80, 0, 32 * 4);
    float nan = NAN;
    memcpy(rec + 80 + MLXR_MOTION_PRESSED * 4, &nan, 4);
    memcpy(rec + 80, mo, (size_t)moN * 4);
    fseek(f, o, SEEK_SET);
    fwrite(rec, 1, sizeof(rec), f);
    fclose(f);
}

// A part's transform applied to a point
static void movePoint(const MlxrModelPart* p, const float in[3], float out[3]) {
    for (int r = 0; r < 3; r++) out[r] = p->transform[r] * in[0] + p->transform[4 + r] * in[1] + p->transform[8 + r] * in[2] + p->transform[12 + r];
}

static int near3(const float a[3], float x, float y, float z, float eps) {
    return fabsf(a[0] - x) < eps && fabsf(a[1] - y) < eps && fabsf(a[2] - z) < eps;
}

// One real hand's component, by name (NULL if absent)
static int findPart(const MlxrModel* m, int h, const char* name) {
    for (uint32_t c = 0; c < m->componentCount[h]; c++)
        if (!strcmp(m->components[h][c].name, name)) return (int)c;
    return -1;
}

// The part among n that draws the range starting at first
static const MlxrModelPart* partFor(const MlxrModelPart* parts, uint32_t n, uint32_t first) {
    for (uint32_t i = 0; i < n; i++)
        if (parts[i].firstIndex <= first && first < parts[i].firstIndex + parts[i].indexCount) return &parts[i];
    return NULL;
}

static uint32_t lastPosed;
static uint32_t posed(const MlxrModel* m, int h, const MlxrModelInput* in, MlxrModelPart* parts) {
    return lastPosed = mlxr_model_pose(m, h, in, parts, MLXR_MODEL_COMPONENTS_MAX);
}

// The part of the last pose that draws component c
static const MlxrModelPart* partAt(const MlxrModel* m, int h, int c, const MlxrModelPart* parts) {
    uint32_t first = m->components[h][c].firstIndex;
    for (uint32_t i = 0; i < lastPosed; i++)
        if (parts[i].firstIndex <= first && first < parts[i].firstIndex + parts[i].indexCount) return &parts[i];
    return &parts[0];
}

static void testModelPose(void) {
    char left[700], right[700];
    stageModels();
    modelPath(left, sizeof(left), "frame_controller_left.model");
    modelPath(right, sizeof(right), "frame_controller_right.model");
    const float D2R = 3.14159265f / 180.0f;

    // Component 0 a joystick (frame = identity, centre at the origin, +-20 degrees, a 1 mm dip along -y),
    // component 1 a trigger (about x through the origin, 0 to -12.5 degrees, -14 when fully pulled)
    float stick[32] = {0};
    stick[MLXR_MOTION_FRAME + 0] = 1;
    stick[MLXR_MOTION_FRAME + 4] = 1;
    stick[MLXR_MOTION_FRAME + 8] = 1;
    stick[MLXR_MOTION_ROT_X] = 20, stick[MLXR_MOTION_ROT_X + 1] = -20;
    stick[MLXR_MOTION_ROT_Y] = 20, stick[MLXR_MOTION_ROT_Y + 1] = -20;
    stick[MLXR_MOTION_PRESS + 1] = -0.001f;
    float trig[32] = {0};
    trig[MLXR_MOTION_AXIS] = 1;
    trig[MLXR_MOTION_RANGE + 1] = -12.5f;
    trig[MLXR_MOTION_PRESSED] = -14.0f;
    for (int h = 0; h < 2; h++) {
        const char* path = h ? right : left;
        writeQuadModel(path, 0.0f, 0, 2);
        patchMotion(path, 0, "/input/thumbstick", MLXR_MOTION_JOYSTICK, stick, 32);
        patchMotion(path, 1, "/input/trigger", MLXR_MOTION_ROTATE, trig, 32);
    }
    MlxrModel* m = NULL;
    check(mlxr_model_load(&gpu, kTexDir, &m) == 0 && m && m->textured, "load the pose test model (MXFM 2)");
    if (!m) return;
    check(m->components[0][0].input == MLXR_IN_STICK_X && m->components[0][1].input == MLXR_IN_TRIGGER, "paths find their controls");
    MlxrModelInput in;
    MlxrModelPart parts[MLXR_MODEL_COMPONENTS_MAX];
    memset(&in, 0, sizeof(in));
    check(posed(m, 0, &in, parts) == 1 && parts[0].firstIndex == 0 && parts[0].indexCount == 6,
          "at rest the whole hand is one draw (parts at rest next to each other merge), absolute ranges");
    float tip[3] = {0, 0.01f, 0}, o[3];
    movePoint(partAt(m, 0, 0, parts), tip, o);
    check(near3(o, 0, 0.01f, 0, 1e-6f), "at rest the stick does not move");
    check(mlxr_model_pose(m, 0, &in, parts, 1) == 1, "cap respected");
    check(mlxr_model_pose(m, 0, &in, NULL, 4) == 0 && mlxr_model_pose(NULL, 0, &in, parts, 4) == 0, "no parts, no model");
    float s20 = sinf(20 * D2R), c20 = cosf(20 * D2R);
    in.v[MLXR_IN_STICK_X] = 1; // right: the stick's top leans to +x
    check(posed(m, 0, &in, parts) == 2 && parts[0].indexCount == 3 && parts[1].firstIndex == 3, "a moved part splits the draw");
    movePoint(partAt(m, 0, 0, parts), tip, o);
    check(near3(o, 0.01f * s20, 0.01f * c20, 0, 1e-6f), "stick right: tip %.5f %.5f %.5f", o[0], o[1], o[2]);
    in.v[MLXR_IN_STICK_X] = 0;
    in.v[MLXR_IN_STICK_Y] = 1; // forward: leans to -z
    posed(m, 0, &in, parts);
    movePoint(partAt(m, 0, 0, parts), tip, o);
    check(near3(o, 0, 0.01f * c20, -0.01f * s20, 1e-6f), "stick forward: tip %.5f %.5f %.5f", o[0], o[1], o[2]);
    in.v[MLXR_IN_STICK_X] = 1;
    in.v[MLXR_IN_STICK_Y] = 1; // a corner counts as the rim: no more than the full tilt
    posed(m, 0, &in, parts);
    movePoint(partAt(m, 0, 0, parts), tip, o);
    float tilt = acosf(o[1] / sqrtf(o[0] * o[0] + o[1] * o[1] + o[2] * o[2])) / D2R;
    check(tilt > 19.0f && tilt < 20.5f, "a diagonal at the rim tilts no more than %.1f degrees (%.2f)", 20.0f, tilt);
    memset(&in, 0, sizeof(in));
    in.v[MLXR_IN_STICK_CLICK] = 1;
    posed(m, 0, &in, parts);
    movePoint(partAt(m, 0, 0, parts), tip, o);
    check(near3(o, 0, 0.01f - 0.001f, 0, 1e-6f), "stick click dips along the press offset");
    // The trigger: about x through the origin, a point below the pivot swings back (+z)
    float below[3] = {0, -0.02f, 0};
    memset(&in, 0, sizeof(in));
    in.v[MLXR_IN_TRIGGER] = 0.5f;
    posed(m, 0, &in, parts);
    movePoint(partAt(m, 0, 1, parts), below, o);
    float a = -6.25f * D2R;
    check(near3(o, 0, -0.02f * cosf(a), -0.02f * sinf(a), 1e-6f) && o[2] > 0, "trigger half: %.5f %.5f %.5f", o[0], o[1], o[2]);
    in.v[MLXR_IN_TRIGGER] = 1;
    posed(m, 0, &in, parts);
    movePoint(partAt(m, 0, 1, parts), below, o);
    a = -14.0f * D2R; // the pressed override at the end of its travel
    check(near3(o, 0, -0.02f * cosf(a), -0.02f * sinf(a), 1e-6f), "trigger full: %.5f %.5f %.5f", o[0], o[1], o[2]);
    mlxr_model_unload(&gpu, m);

    // Component 0 a d-pad, component 1 a button sliding 1.4 mm along -y
    float dpad[32];
    memcpy(dpad, stick, sizeof(dpad));
    dpad[MLXR_MOTION_ROT_X] = 7, dpad[MLXR_MOTION_ROT_X + 1] = -7;
    dpad[MLXR_MOTION_ROT_Y] = 7, dpad[MLXR_MOTION_ROT_Y + 1] = -7;
    float button[32] = {0};
    button[MLXR_MOTION_AXIS + 1] = -1;
    button[MLXR_MOTION_RANGE + 1] = 0.0014f;
    for (int h = 0; h < 2; h++) {
        const char* path = h ? right : left;
        writeQuadModel(path, 0.0f, 0, 2);
        patchMotion(path, 0, "/input/dpad_up/click", MLXR_MOTION_DPAD, dpad, 32);
        patchMotion(path, 1, "/input/a/click", MLXR_MOTION_TRANSLATE, button, 32);
    }
    check(mlxr_model_load(&gpu, kTexDir, &m) == 0 && m && m->textured, "load the d-pad test model");
    if (!m) return;
    check(m->components[0][0].input == MLXR_IN_DPAD_UP && m->components[0][1].input == MLXR_IN_A, "d-pad and button paths");
    memset(&in, 0, sizeof(in));
    in.v[MLXR_IN_DPAD_RIGHT] = 1;
    posed(m, 0, &in, parts);
    movePoint(partAt(m, 0, 0, parts), tip, o);
    float s7 = sinf(7 * D2R);
    check(o[0] > 0.01f * s7 * 0.99f && fabsf(o[2]) < 1e-6f && o[1] < 0.01f, "d-pad right tilts toward +x and dips: %.5f %.5f %.5f",
          o[0], o[1], o[2]);
    memset(&in, 0, sizeof(in));
    in.v[MLXR_IN_DPAD_UP] = 1;
    posed(m, 0, &in, parts);
    movePoint(partAt(m, 0, 0, parts), tip, o);
    check(o[2] < -0.01f * s7 * 0.99f && fabsf(o[0]) < 1e-6f, "d-pad up tilts forward (-z): %.5f %.5f %.5f", o[0], o[1], o[2]);
    memset(&in, 0, sizeof(in));
    in.v[MLXR_IN_A] = 1;
    float origin[3] = {0, 0, 0};
    posed(m, 0, &in, parts);
    movePoint(partAt(m, 0, 1, parts), origin, o);
    check(near3(o, 0, -0.0014f, 0, 1e-7f), "button A slides along its axis by its range");
    in.v[MLXR_IN_A] = 0;
    posed(m, 0, &in, parts);
    movePoint(partAt(m, 0, 1, parts), origin, o);
    check(near3(o, 0, 0, 0, 1e-9f), "released, it is back");
    mlxr_model_unload(&gpu, m);

    // An MXFM 1 file has no d-pad motion: type 4 in it is refused (the textured pair is skipped)
    for (int h = 0; h < 2; h++) {
        const char* path = h ? right : left;
        writeQuadModel(path, 0.0f, 0, 1);
        patchMotion(path, 0, "/input/dpad_up/click", MLXR_MOTION_DPAD, dpad, 32);
    }
    check(mlxr_model_load(&gpu, kTexDir, &m) != 0 || !m || !m->textured, "a version 1 file with a d-pad motion is refused");
    if (m) mlxr_model_unload(&gpu, m);
    m = NULL;
    // ... and without one it still loads and draws, its parts all at rest
    writeQuadModel(left, 0.0f, 0, 1);
    writeQuadModel(right, 0.0f, 0, 1);
    check(mlxr_model_load(&gpu, kTexDir, &m) == 0 && m && m->textured, "an MXFM 1 file still loads");
    if (m) {
        memset(&in, 0, sizeof(in));
        in.v[MLXR_IN_TRIGGER] = 1;
        check(posed(m, 0, &in, parts) == 1 && parts[0].indexCount == 6, "and poses (no motion in it: one draw)");
        mlxr_model_unload(&gpu, m);
    }

    // The real models
    char rl[700], rr[700];
    if (frameModelsDir) {
        snprintf(rl, sizeof(rl), "%s/frame_controller_left.model", frameModelsDir);
        snprintf(rr, sizeof(rr), "%s/frame_controller_right.model", frameModelsDir);
    }
    if (!frameModelsDir || access(rl, R_OK) != 0 || access(rr, R_OK) != 0) {
        note("pose         no converted Steam Frame models here (no SteamVR?): the real ones were not posed");
        return;
    }
    char cmd[2400];
    stageModels();
    snprintf(cmd, sizeof(cmd), "cp '%s' '%s' '%s/models/'", rl, rr, kTexDir);
    check(system(cmd) == 0, "stage the real models");
    check(mlxr_model_load(&gpu, kTexDir, &m) == 0 && m && m->textured, "load the real pair");
    if (!m) return;
    static const struct { const char* name; int slot; int hands; } kMoving[] = {
        {"trigger", MLXR_IN_TRIGGER, 3}, {"button_grip", MLXR_IN_GRIP, 3}, {"bumper", MLXR_IN_BUMPER, 3},
        {"thumbstick", MLXR_IN_STICK_X, 3}, {"button_a", MLXR_IN_A, 2}, {"button_b", MLXR_IN_B, 2},
        {"button_x", MLXR_IN_X, 2}, {"button_y", MLXR_IN_Y, 2}, {"button_menu", MLXR_IN_MENU, 2},
        {"button_view", MLXR_IN_VIEW, 1}, {"dpad", MLXR_IN_DPAD_UP, 1}, {"button_system", MLXR_IN_SYSTEM, 3},
    };
    for (size_t k = 0; k < sizeof(kMoving) / sizeof(kMoving[0]); k++) {
        for (int h = 0; h < 2; h++) {
            if (!(kMoving[k].hands & (1 << h))) continue;
            int c = findPart(m, h, kMoving[k].name);
            check(c >= 0 && m->components[h][c].motionType != MLXR_MOTION_NONE && m->components[h][c].input >= 0,
                  "%s hand: %s has a motion and a control", h ? "right" : "left", kMoving[k].name);
            if (c < 0) continue;
            MlxrModelInput rest, pressed;
            memset(&rest, 0, sizeof(rest));
            memset(&pressed, 0, sizeof(pressed));
            pressed.v[kMoving[k].slot] = 1;
            if (kMoving[k].slot == MLXR_IN_STICK_X) pressed.v[MLXR_IN_STICK_X] = 1;
            MlxrModelPart pr[MLXR_MODEL_COMPONENTS_MAX], pp[MLXR_MODEL_COMPONENTS_MAX];
            uint32_t n = mlxr_model_pose(m, h, &rest, pr, MLXR_MODEL_COMPONENTS_MAX);
            mlxr_model_pose(m, h, &pressed, pp, MLXR_MODEL_COMPONENTS_MAX);
            // merged at rest, split when moved: find each by the range it draws
            uint32_t n2 = mlxr_model_pose(m, h, &pressed, pp, MLXR_MODEL_COMPONENTS_MAX);
            const MlxrModelPart* a = partFor(pr, n, m->components[h][c].firstIndex);
            const MlxrModelPart* b = partFor(pp, n2, m->components[h][c].firstIndex);
            int moved = 0;
            for (int e = 0; a && b && e < 16; e++) if (fabsf(a->transform[e] - b->transform[e]) > 1e-6f) moved = 1;
            // and the rest of the hand did not
            int c0 = findPart(m, h, "body");
            const MlxrModelPart* body = partFor(pp, n2, m->components[h][c0].firstIndex);
            for (int e = 0; body && e < 16; e++) if (fabsf(body->transform[e] - (e % 5 == 0 ? 1.0f : 0.0f)) > 0) moved = 0;
            check(moved, "%s hand: %s moves when its control does", h ? "right" : "left", kMoving[k].name);
            if (kMoving[k].slot == MLXR_IN_SYSTEM) {
                // nothing sets it in the client (reserved by the runtime): the model's button stays up
                check(1, "(the system button moves only if a runtime gives the app its value)");
            }
        }
    }
    // Directions on the real thumbstick, in grip space: right leans to +x, forward to -z, a click sinks it
    for (int h = 0; h < 2; h++) {
        int c = findPart(m, h, "thumbstick");
        if (c < 0) continue;
        const float* mo = m->components[h][c].motion;
        float top[3] = {mo[MLXR_MOTION_ORIGIN] + mo[MLXR_MOTION_FRAME + 3] * 0.01f,
                        mo[MLXR_MOTION_ORIGIN + 1] + mo[MLXR_MOTION_FRAME + 4] * 0.01f,
                        mo[MLXR_MOTION_ORIGIN + 2] + mo[MLXR_MOTION_FRAME + 5] * 0.01f};
        memset(&in, 0, sizeof(in));
        MlxrModelPart pt[MLXR_MODEL_COMPONENTS_MAX];
        in.v[MLXR_IN_STICK_X] = 1;
        uint32_t n = mlxr_model_pose(m, h, &in, pt, MLXR_MODEL_COMPONENTS_MAX);
        const MlxrModelPart* sp = partFor(pt, n, m->components[h][c].firstIndex);
        float o2[3] = {0};
        if (sp) movePoint(sp, top, o2);
        check(sp && o2[0] - top[0] > 0.002f && fabsf(o2[2] - top[2]) < 0.002f, "%s thumbstick right moves its top to +x (%.4f)",
              h ? "right" : "left", o2[0] - top[0]);
        in.v[MLXR_IN_STICK_X] = 0;
        in.v[MLXR_IN_STICK_Y] = 1;
        n = mlxr_model_pose(m, h, &in, pt, MLXR_MODEL_COMPONENTS_MAX);
        sp = partFor(pt, n, m->components[h][c].firstIndex);
        if (sp) movePoint(sp, top, o2);
        check(sp && o2[2] - top[2] < -0.002f, "%s thumbstick forward moves its top to -z (%.4f)", h ? "right" : "left",
              o2[2] - top[2]);
    }
    // Rendered with the trigger pulled, the stick tilted and every button down: the picture differs from rest
    Frame fr, fp;
    ModelPose rest;
    memset(&rest, 0, sizeof(rest));
    Vec3 up = {0, 1, 0}, ax = {1, 0, 0};
    for (int h = 0; h < 2; h++) {
        XrQuaternionf yaw = axisAngleQuat(up, h ? -0.6f : 0.6f);
        rest.grip[h].orientation = quatMul(yaw, axisAngleQuat(ax, 0.6f));
        rest.grip[h].position = (XrVector3f){h ? 0.06f : -0.06f, -0.03f, -0.16f};
        rest.shown[h] = 1;
    }
    rest.animate = 1;
    ModelPose down = rest;
    for (int h = 0; h < 2; h++) {
        for (int k = 0; k < MLXR_IN_COUNT; k++) down.input[h].v[k] = 1.0f;
        down.input[h].v[MLXR_IN_STICK_X] = 0.8f;
        down.input[h].v[MLXR_IN_STICK_Y] = -0.6f;
    }
    check(renderModelsAt(m, 512, 512, &rest, &fr) == 0 && renderModelsAt(m, 512, 512, &down, &fp) == 0, "render rest and pressed");
    size_t px = (size_t)512 * 2 * 512, diff = 0, covered = 0;
    for (size_t i = 0; i < px; i++) {
        covered += fr.rgba[i * 4 + 3] > 0;
        int dd = 0;
        for (int c = 0; c < 3; c++) dd += abs((int)fr.rgba[i * 4 + c] - (int)fp.rgba[i * 4 + c]);
        diff += dd > 12;
    }
    note("pose         rendered pressed vs rest: %zu pixels differ of %zu covered", diff, covered);
    check(covered > 20000 && diff > 300 && diff < covered / 2, "pressing changes the picture a little, not the whole model");
    uint8_t* comp = malloc(px * 4);
    compositeOver(fr.rgba, NULL, comp, px);
    writePng("model_pose_rest", comp, 1024, 512);
    compositeOver(fp.rgba, NULL, comp, px);
    writePng("model_pose_pressed", comp, 1024, 512);
    free(comp);
    frameFree(&fr);
    frameFree(&fp);
    mlxr_model_unload(&gpu, m);
}

// What loads when the files are missing or broken: the textured pair first,
// then the painted pair, then the generic controller
static void testModelFallback(void) {
    char tl[700], tr[700], pl[700], pr[700], gen[700];
    modelPath(tl, sizeof(tl), "frame_controller_left.model");
    modelPath(tr, sizeof(tr), "frame_controller_right.model");
    modelPath(pl, sizeof(pl), "frame_controller_left.room");
    modelPath(pr, sizeof(pr), "frame_controller_right.room");
    modelPath(gen, sizeof(gen), "controller.room");
    struct {
        const char* what;
        int left, right; // the .model halves: 0 none, 1 good, 2 cut short, 3 another version
        int painted;     // the .room pair
        int generic;     // controller.room
        int want;        // 0 generic, 1 painted pair, 2 textured pair, -1 nothing loads
    } cases[] = {
        {"nothing of the Frame's", 0, 0, 0, 1, 0},
        {"the textured pair", 1, 1, 0, 1, 2},
        {"the textured pair over the painted one", 1, 1, 1, 1, 2},
        {"the textured pair without the generic controller", 1, 1, 0, 0, 2},
        {"one textured half, the painted pair", 1, 0, 1, 1, 1},
        {"one textured half", 0, 1, 0, 1, 0},
        {"a textured half cut short", 1, 2, 0, 1, 0},
        {"a textured half of an unknown version, the painted pair", 3, 1, 1, 1, 1},
        {"a textured half cut short, nothing else", 2, 1, 0, 0, -1},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        stageModels();
        if (cases[i].left) writeQuadModel(tl, 0.0f, cases[i].left == 2, cases[i].left == 3 ? 3 : 1);
        if (cases[i].right) writeQuadModel(tr, 0.0f, cases[i].right == 2, cases[i].right == 3 ? 3 : 1);
        if (cases[i].painted) {
            writeTriRoom(pl);
            writeTriRoom(pr);
        }
        if (!cases[i].generic) remove(gen);
        MlxrModel* m = NULL;
        int rc = mlxr_model_load(&gpu, kTexDir, &m);
        int got = rc != 0 || !m ? -1 : m->textured ? 2 : m->perHand ? 1 : 0;
        note("fallback     %-55s -> %s", cases[i].what,
             got == 2 ? "textured pair" : got == 1 ? "painted pair" : got == 0 ? "generic" : "nothing");
        check(got == cases[i].want, "%s: got %d, want %d", cases[i].what, got, cases[i].want);
        if (m) {
            // whatever loaded draws
            Frame f;
            check(renderModels(m, 128, 128, &f) == 0, "%s: draws", cases[i].what);
            frameFree(&f);
            mlxr_model_unload(&gpu, m);
        }
    }
}

static void testOverlay(void) {
    MlxrRoom* r = loadRoom(ROOM_STYLE_THEATER, 0, 0.0f);
    MlxrModel* m = NULL;
    mlxr_model_load(&gpu, MLXR_ASSETS_DIR, &m);
    if (!r || !m) return;
    Frame room, hands;
    const uint32_t EW = 1024, EH = 1024;
    check(renderRoom(r, EW, EH, 2, 1, 1, &room) == 0 && renderModels(m, EW, EH, &hands) == 0, "render room and models at one size");
    const size_t n = (size_t)EW * 2 * EH;
    uint8_t* comp = malloc(n * 4);
    compositeOver(hands.rgba, room.rgba, comp, n);
    size_t changed = 0;
    for (size_t i = 0; i < n; i++)
        if (hands.rgba[i * 4 + 3] == 255 && memcmp(comp + i * 4, room.rgba + i * 4, 3) != 0) changed++;
    check(changed > 1000, "the models sit over the room (%zu pixels)", changed);
    writePng("overlay_home_theater", comp, EW * 2, EH);
    free(comp);
    frameFree(&room);
    frameFree(&hands);
    mlxr_room_unload(&gpu, r);
    mlxr_model_unload(&gpu, m);
}

int main(int argc, char** argv) {
    const char* group = argc > 1 ? argv[1] : "all";
    if (argc > 2) outDir = argv[2];
    if (argc > 3) frameModelsDir = argv[3];
    int all = strcmp(group, "all") == 0;
    int did = 0;
    if (all || !strcmp(group, "logic")) {
        testLogic();
        did = 1;
    }
    int needVk = all || strcmp(group, "logic") != 0;
    if (needVk) {
        int vk = vkInit();
        if (vk != 0) {
            fprintf(stderr, "no usable Vulkan 1.2 device with dynamic rendering\n");
            return vk == 77 ? 77 : 1;
        }
        note("device: %s (Vulkan %u.%u.%u), outdir %s", props.deviceName, VK_VERSION_MAJOR(props.apiVersion),
             VK_VERSION_MINOR(props.apiVersion), VK_VERSION_PATCH(props.apiVersion), outDir);
    }
    if (all || !strcmp(group, "load")) { if (needAstc()) testLoad(); did = 1; }
    if (all || !strcmp(group, "rescale")) { if (needAstc()) testRescale(); did = 1; }
    if (all || !strcmp(group, "render_home_theater")) { if (needAstc()) testRenderOne(1); did = 1; }
    if (all || !strcmp(group, "render_grand_cinema")) { if (needAstc()) testRenderOne(2); did = 1; }
    if (all || !strcmp(group, "render_synthwave")) { if (needAstc()) testRenderOne(3); did = 1; }
    if (all || !strcmp(group, "model")) { testModel(); did = 1; }
    if (all || !strcmp(group, "overlay")) { if (needAstc()) testOverlay(); did = 1; }
    if (all || !strcmp(group, "model_textured")) { testModelTextured(); did = 1; }
    if (all || !strcmp(group, "model_pose")) { testModelPose(); did = 1; }
    if (all || !strcmp(group, "model_fallback")) { testModelFallback(); did = 1; }
    if (!did) {
        fprintf(stderr, "unknown group %s\n", group);
        return 2;
    }
    if (needVk) {
        vkDeviceWaitIdle(gpu.device);
        if (sharedMade) {
            imgFree(&ambiImg);
            imgFree(&shotImg);
        }
        mlxr_passes_destroy(passes);
        vkDestroyCommandPool(gpu.device, pool, NULL);
        vkDestroyDevice(gpu.device, NULL);
        vkDestroyInstance(instance, NULL);
    }
    printf("%s: %d checks, %d failed\n", group, run, failed);
    if (astcSkipped) printf("%d group(s) skipped: no textureCompressionASTC_LDR for the rooms' atlases\n", astcSkipped);
    if (failed) return 1;
    return astcSkipped && !all ? 77 : 0;
}
