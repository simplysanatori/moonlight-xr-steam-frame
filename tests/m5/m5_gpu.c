// Moonlight XR Native, tests/m5: the headless device (see m5_gpu.h). GPLv3.
#include "m5_gpu.h"

#include <stdio.h>

int m5_gpu_init(M5Gpu* g) {
    memset(g, 0, sizeof(*g));
    XrVk* vk = &g->vk;
    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = "mlxr-m5-test";
    app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ici.pApplicationInfo = &app;
    if (vkCreateInstance(&ici, NULL, &vk->instance) != VK_SUCCESS) return -1;
    uint32_t n = 0;
    VkPhysicalDevice devs[8];
    vkEnumeratePhysicalDevices(vk->instance, &n, NULL);
    if (n == 0) return -1;
    if (n > 8) n = 8;
    vkEnumeratePhysicalDevices(vk->instance, &n, devs);
    vk->phys = devs[0];
    vkGetPhysicalDeviceProperties(vk->phys, &g->props);
    vkGetPhysicalDeviceMemoryProperties(vk->phys, &vk->mem);
    uint32_t nq = 0;
    VkQueueFamilyProperties qf[16];
    vkGetPhysicalDeviceQueueFamilyProperties(vk->phys, &nq, NULL);
    if (nq > 16) nq = 16;
    vkGetPhysicalDeviceQueueFamilyProperties(vk->phys, &nq, qf);
    vk->queueFamily = UINT32_MAX;
    for (uint32_t i = 0; i < nq; i++)
        if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            vk->queueFamily = i;
            break;
        }
    if (vk->queueFamily == UINT32_MAX) return -1;
    // As xr_vk.c asks for them
    const char* exts[] = { VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME };
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    qci.queueFamilyIndex = vk->queueFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    VkPhysicalDeviceDynamicRenderingFeaturesKHR dr = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR };
    dr.dynamicRendering = VK_TRUE;
    VkPhysicalDeviceVulkan11Features f11 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
    f11.pNext = &dr;
    f11.samplerYcbcrConversion = VK_TRUE;
    VkPhysicalDeviceVulkan12Features f12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    f12.pNext = &f11;
    f12.timelineSemaphore = VK_TRUE;
    f12.hostQueryReset = VK_TRUE;
    VkPhysicalDeviceFeatures2 f2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    f2.pNext = &f12;
    f2.features.textureCompressionASTC_LDR = VK_TRUE;
    f2.features.samplerAnisotropy = VK_TRUE;
    VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    dci.pNext = &f2;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 2;
    dci.ppEnabledExtensionNames = exts;
    if (vkCreateDevice(vk->phys, &dci, NULL, &vk->device) != VK_SUCCESS) return -1;
    vkGetDeviceQueue(vk->device, vk->queueFamily, 0, &vk->queue);
    VkCommandPoolCreateInfo pci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = vk->queueFamily;
    if (vkCreateCommandPool(vk->device, &pci, NULL, &g->pool) != VK_SUCCESS) return -1;
    VkSemaphoreTypeCreateInfo st = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
    st.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkSemaphoreCreateInfo sci = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    sci.pNext = &st;
    if (vkCreateSemaphore(vk->device, &sci, NULL, &g->timeline) != VK_SUCCESS) return -1;
    // As xr_session.c makes them
    MlxrPassesInfo pi;
    memset(&pi, 0, sizeof(pi));
    pi.phys = vk->phys;
    pi.device = vk->device;
    pi.nv12 = 1;
    pi.nv12Chroma = MLXR_CHROMA_LEFT;
    pi.atlasAnisotropy = ROOM_ANISOTROPY_MAX;
    if (mlxr_passes_create(&pi, &g->passes) != 0) return -1;
    VkCommandBuffer cmd = m5_begin(g);
    mlxr_passes_prepare(g->passes, cmd);
    return m5_submit(g, cmd, NULL, 0);
}

void m5_gpu_free(M5Gpu* g) {
    if (g->vk.device) {
        vkDeviceWaitIdle(g->vk.device);
        if (g->passes) mlxr_passes_destroy(g->passes);
        if (g->timeline) vkDestroySemaphore(g->vk.device, g->timeline, NULL);
        if (g->pool) vkDestroyCommandPool(g->vk.device, g->pool, NULL);
        vkDestroyDevice(g->vk.device, NULL);
    }
    if (g->vk.instance) vkDestroyInstance(g->vk.instance, NULL);
    memset(g, 0, sizeof(*g));
}

VkCommandBuffer m5_begin(M5Gpu* g) {
    VkCommandBufferAllocateInfo cai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cai.commandPool = g->pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(g->vk.device, &cai, &cmd);
    VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    return cmd;
}

int m5_submit(M5Gpu* g, VkCommandBuffer cmd, const VkCommandBuffer* extra, int nExtra) {
    vkEndCommandBuffer(cmd);
    VkCommandBuffer cmds[8];
    int n = 0;
    for (int i = 0; i < nExtra && n < 7; i++) cmds[n++] = extra[i];
    cmds[n++] = cmd;
    uint64_t value = ++g->serial;
    VkTimelineSemaphoreSubmitInfo tsi = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
    tsi.signalSemaphoreValueCount = 1;
    tsi.pSignalSemaphoreValues = &value;
    VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.pNext = &tsi;
    si.commandBufferCount = (uint32_t)n;
    si.pCommandBuffers = cmds;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &g->timeline;
    int rc = vkQueueSubmit(g->vk.queue, 1, &si, VK_NULL_HANDLE) == VK_SUCCESS ? 0 : -1;
    VkSemaphoreWaitInfo wi = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
    wi.semaphoreCount = 1;
    wi.pSemaphores = &g->timeline;
    wi.pValues = &value;
    if (rc == 0 && vkWaitSemaphores(g->vk.device, &wi, 30000000000ull) != VK_SUCCESS) rc = -1;
    vkFreeCommandBuffers(g->vk.device, g->pool, 1, &cmd);
    return rc;
}

void m5_ctx_gpu(M5Gpu* g, XrCtx* ctx) {
    ctx->vk = &g->vk;
    ctx->passes = g->passes;
    ctx->offscreen = 1;
    ctx->recommendedEyeWidth = 1728;
    ctx->recommendedEyeHeight = 1728;
    ctx->maxEyeWidth = 8192;
    ctx->maxEyeHeight = 8192;
    snprintf(ctx->assetsDir, sizeof(ctx->assetsDir), "%s", MLXR_ASSETS_DIR);
}

void m5_frame(M5Gpu* g, XrCtx* ctx, void (*body)(XrCtx* ctx, VkCommandBuffer cmd, void* user), void* user) {
    VkCommandBuffer cmd = m5_begin(g);
    ctx->frameCmd = cmd;
    ctx->gpuSerial = g->serial + 1;
    xrWorldFrameBegin(ctx);
    if (body) body(ctx, cmd, user);
    VkCommandBuffer up[4];
    int n = xrWorldTakeUploads(ctx, up, 4);
    ctx->frameCmd = VK_NULL_HANDLE;
    m5_submit(g, cmd, up, n);
    xrWorldSubmitted(ctx, g->serial);
    ctx->gpuDone = g->serial;
}

int m5_image(M5Gpu* g, XrCtx* ctx, M5Image* out, int w, int h, VkFormat format, VkImageUsageFlags usage,
             VkImageAspectFlags aspect) {
    (void)g;
    out->w = w;
    out->h = h;
    return xrVkImageMake(ctx, &out->img, w, h, format, usage, aspect) ? 0 : -1;
}

void m5_image_free(XrCtx* ctx, M5Image* im) {
    xrVkImageFree(ctx, &im->img);
}

typedef struct {
    VkBuffer buf;
    VkDeviceMemory mem;
    void* map;
} HostBuf;

static int hostBuf(M5Gpu* g, VkDeviceSize size, VkBufferUsageFlags usage, HostBuf* b) {
    VkBufferCreateInfo bi = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bi.size = size;
    bi.usage = usage;
    if (vkCreateBuffer(g->vk.device, &bi, NULL, &b->buf) != VK_SUCCESS) return -1;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(g->vk.device, b->buf, &req);
    VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = xr_vk_memory_type(&g->vk, req.memoryTypeBits,
                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(g->vk.device, &ai, NULL, &b->mem) != VK_SUCCESS) return -1;
    vkBindBufferMemory(g->vk.device, b->buf, b->mem, 0);
    vkMapMemory(g->vk.device, b->mem, 0, VK_WHOLE_SIZE, 0, &b->map);
    return 0;
}

static void hostFree(M5Gpu* g, HostBuf* b) {
    vkDestroyBuffer(g->vk.device, b->buf, NULL);
    vkFreeMemory(g->vk.device, b->mem, NULL);
}

int m5_readback(M5Gpu* g, VkImage image, VkImageLayout layout, int w, int h, uint8_t* rgba) {
    HostBuf b;
    if (hostBuf(g, (VkDeviceSize)w * h * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &b) != 0) return -1;
    VkCommandBuffer cmd = m5_begin(g);
    mlxr_barrier(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy rg;
    memset(&rg, 0, sizeof(rg));
    rg.imageSubresource = (VkImageSubresourceLayers){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    rg.imageExtent = (VkExtent3D){ (uint32_t)w, (uint32_t)h, 1 };
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, b.buf, 1, &rg);
    mlxr_barrier(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, layout);
    int rc = m5_submit(g, cmd, NULL, 0);
    memcpy(rgba, b.map, (size_t)w * h * 4);
    hostFree(g, &b);
    return rc;
}

int m5_upload(M5Gpu* g, VkImage image, int w, int h, int bpp, const void* bytes) {
    HostBuf b;
    if (hostBuf(g, (VkDeviceSize)w * h * bpp, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &b) != 0) return -1;
    memcpy(b.map, bytes, (size_t)w * h * bpp);
    VkCommandBuffer cmd = m5_begin(g);
    mlxr_barrier(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy rg;
    memset(&rg, 0, sizeof(rg));
    rg.imageSubresource = (VkImageSubresourceLayers){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    rg.imageExtent = (VkExtent3D){ (uint32_t)w, (uint32_t)h, 1 };
    vkCmdCopyBufferToImage(cmd, b.buf, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &rg);
    mlxr_barrier(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    int rc = m5_submit(g, cmd, NULL, 0);
    hostFree(g, &b);
    return rc;
}

static uint8_t clampByte(float v) {
    return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v + 0.5f);
}

int m5_video(M5Gpu* g, XrCtx* ctx, M5Video* v, int w, int h, void (*rgb)(float u, float v, float out[3])) {
    memset(v, 0, sizeof(*v));
    int cw = w / 2, ch = h / 2;
    uint8_t* y = malloc((size_t)w * h);
    uint8_t* cb = malloc((size_t)cw * ch);
    uint8_t* cr = malloc((size_t)cw * ch);
    float* full = malloc(sizeof(float) * 3 * (size_t)w * h);
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            float* c = &full[((size_t)j * w + i) * 3];
            rgb((i + 0.5f) / w, (j + 0.5f) / h, c);
            float Y = 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
            y[(size_t)j * w + i] = clampByte(16.0f + 219.0f * Y);
        }
    }
    for (int j = 0; j < ch; j++) {
        for (int i = 0; i < cw; i++) {
            // left sited: the even column, midway between the rows
            float r = 0, gg = 0, b = 0;
            for (int k = 0; k < 2; k++) {
                const float* c = &full[((size_t)(2 * j + k) * w + 2 * i) * 3];
                r += c[0] * 0.5f;
                gg += c[1] * 0.5f;
                b += c[2] * 0.5f;
            }
            float Y = 0.2126f * r + 0.7152f * gg + 0.0722f * b;
            cb[(size_t)j * cw + i] = clampByte(128.0f + 224.0f * (b - Y) / 1.8556f);
            cr[(size_t)j * cw + i] = clampByte(128.0f + 224.0f * (r - Y) / 1.5748f);
        }
    }
    int sz[3][2] = { { w, h }, { cw, ch }, { cw, ch } };
    const uint8_t* data[3] = { y, cb, cr };
    int rc = 0;
    for (int p = 0; p < 3 && rc == 0; p++) {
        rc = m5_image(g, ctx, &v->planes[p], sz[p][0], sz[p][1], VK_FORMAT_R8_UNORM,
                      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        if (rc == 0) rc = m5_upload(g, v->planes[p].img.image, sz[p][0], sz[p][1], 1, data[p]);
    }
    free(y);
    free(cb);
    free(cr);
    free(full);
    v->video.kind = MLXR_VIDEO_YUV420P;
    for (int p = 0; p < 3; p++) v->video.planes[p] = v->planes[p].img.view;
    v->video.width = (uint32_t)w;
    v->video.height = (uint32_t)h;
    v->video.chroma = MLXR_CHROMA_LEFT;
    return rc;
}

void m5_video_free(XrCtx* ctx, M5Video* v) {
    for (int p = 0; p < 3; p++) m5_image_free(ctx, &v->planes[p]);
}

XrView m5_view(float hfovDeg, int w, int h, float eyeX) {
    XrView v;
    memset(&v, 0, sizeof(v));
    v.type = XR_TYPE_VIEW;
    float tx = tanf(hfovDeg * (float)M_PI / 360.0f);
    float ty = tx * (float)h / (float)w;
    v.fov.angleLeft = -atanf(tx);
    v.fov.angleRight = atanf(tx);
    v.fov.angleUp = atanf(ty);
    v.fov.angleDown = -atanf(ty);
    v.pose.orientation.w = 1.0f;
    v.pose.position.x = eyeX;
    return v;
}
