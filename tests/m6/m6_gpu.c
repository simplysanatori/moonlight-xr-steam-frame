// Moonlight XR Native, tests/m6: see m6_gpu.h. GPLv3.
#include "m6_gpu.h"

#include <stdio.h>

#include "settings.h"
#include "stb_image_write.h"
#include "vk_import.h"
#include "xr_native.h"

int m6_gpu_init(M5Gpu* g, int withImport) {
    memset(g, 0, sizeof(*g));
    XrVk* vk = &g->vk;
    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = "mlxr-m6-test";
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
    // ASTC and anisotropy where the device has them, as m5_gpu.c (lavapipe
    // may have no ASTC)
    VkPhysicalDeviceFeatures have;
    vkGetPhysicalDeviceFeatures(vk->phys, &have);
    g->astc = have.textureCompressionASTC_LDR == VK_TRUE;
    if (!g->astc) fprintf(stderr, "no textureCompressionASTC_LDR on %s\n", g->props.deviceName);
    const char* exts[32] = { VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME };
    uint32_t ne = 2;
    if (withImport) {
        uint32_t ni = 0;
        const char* const* imp = mlxr_vkimp_device_extensions(&ni);
        for (uint32_t i = 0; i < ni && ne < 32; i++) {
            int dup = 0;
            for (uint32_t j = 0; j < ne; j++) dup |= strcmp(exts[j], imp[i]) == 0;
            if (!dup) exts[ne++] = imp[i];
        }
    }
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
    f2.features.textureCompressionASTC_LDR = have.textureCompressionASTC_LDR;
    f2.features.samplerAnisotropy = have.samplerAnisotropy;
    VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    dci.pNext = &f2;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = ne;
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

XrCtx* m6_ctx(M5Gpu* g, const XrDepthEngineOps* ops, const char* assetsDir, const Settings* s) {
    XrCtx* c = calloc(1, sizeof(XrCtx));
    xrCtxDefaults(c, 1920, 1080, 60);
    m5_ctx_gpu(g, c);
    if (assetsDir != NULL) snprintf(c->assetsDir, sizeof(c->assetsDir), "%s", assetsDir);
    c->gpuTimeline = g->timeline;
    xrDepthSetEngineOps(ops);
    xrApplySettings(c, s);
    c->sessionState = XR_SESSION_STATE_FOCUSED;
    c->shouldRender = 1;
    return c;
}

void m6_ctx_free(XrCtx* ctx) {
    vkDeviceWaitIdle(ctx->vk->device);
    xrDepthDestroy(ctx);
    xrWorldDestroy(ctx);
    free(ctx);
}

#define RING 4
static VkCommandBuffer ring[RING];
static uint64_t ringValue[RING];

static void reap(M5Gpu* g, XrCtx* ctx, uint64_t upTo) {
    if (upTo > 0) {
        VkSemaphoreWaitInfo wi = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
        wi.semaphoreCount = 1;
        wi.pSemaphores = &g->timeline;
        wi.pValues = &upTo;
        vkWaitSemaphores(g->vk.device, &wi, 30000000000ull);
    }
    uint64_t done = 0;
    vkGetSemaphoreCounterValue(g->vk.device, g->timeline, &done);
    for (int i = 0; i < RING; i++) {
        if (ring[i] && ringValue[i] <= done) {
            vkFreeCommandBuffers(g->vk.device, g->pool, 1, &ring[i]);
            ring[i] = VK_NULL_HANDLE;
        }
    }
    ctx->gpuDone = done;
}

void m6_frame(M5Gpu* g, XrCtx* ctx, int inFlight, void (*body)(XrCtx* ctx, VkCommandBuffer cmd, void* user),
              void* user) {
    if (inFlight > RING - 1) inFlight = RING - 1;
    reap(g, ctx, g->serial > (uint64_t)inFlight ? g->serial - (uint64_t)inFlight : 0);
    VkCommandBuffer cmd = m5_begin(g);
    ctx->frameCmd = cmd;
    ctx->gpuSerial = g->serial + 1;
    xrWorldFrameBegin(ctx);
    xrDepthFrame(ctx);
    if (body) body(ctx, cmd, user);
    VkCommandBuffer cmds[5];
    int n = xrWorldTakeUploads(ctx, cmds, 4);
    cmds[n++] = cmd;
    vkEndCommandBuffer(cmd);
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
    vkQueueSubmit(g->vk.queue, 1, &si, VK_NULL_HANDLE);
    ctx->frameCmd = VK_NULL_HANDLE;
    xrWorldSubmitted(ctx, value);
    xrDepthSubmitted(ctx, value);
    int slot = -1;
    for (int i = 0; i < RING; i++)
        if (!ring[i]) {
            slot = i;
            break;
        }
    if (slot < 0) {
        reap(g, ctx, value);
        vkFreeCommandBuffers(g->vk.device, g->pool, 1, &cmd);
    }
    else {
        ring[slot] = cmd;
        ringValue[slot] = value;
    }
    if (inFlight == 0) reap(g, ctx, value);
}

void m6_idle(M5Gpu* g, XrCtx* ctx) {
    reap(g, ctx, g->serial);
}

int m6_png(const char* path, const uint8_t* rgba, int w, int h, int opaque) {
    uint8_t* copy = malloc((size_t)w * h * 4);
    memcpy(copy, rgba, (size_t)w * h * 4);
    if (opaque)
        for (size_t i = 0; i < (size_t)w * h; i++) copy[i * 4 + 3] = 255;
    int ok = stbi_write_png(path, w, h, 4, copy, w * 4);
    free(copy);
    return ok ? 0 : -1;
}
