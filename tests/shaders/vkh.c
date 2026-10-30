// Moonlight XR Native: headless Vulkan helpers for the shader tests. GPLv3.
#include "vkh.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "passes.h"

static uint32_t memType(Vkh* v, uint32_t bits, VkMemoryPropertyFlags want) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(v->phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) return i;
    return UINT32_MAX;
}

int vkh_init(Vkh* v) {
    memset(v, 0, sizeof(*v));
    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "mlxr-shader-test";
    app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    if (vkCreateInstance(&ici, NULL, &v->instance) != VK_SUCCESS) {
        fprintf(stderr, "vkCreateInstance failed\n");
        return VKH_NO_DEVICE;
    }
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(v->instance, &n, NULL);
    if (n == 0) return VKH_NO_DEVICE;
    VkPhysicalDevice devs[8];
    if (n > 8) n = 8;
    vkEnumeratePhysicalDevices(v->instance, &n, devs);
    v->phys = devs[0];
    vkGetPhysicalDeviceProperties(v->phys, &v->props);
    uint32_t nq = 0;
    VkQueueFamilyProperties qf[16];
    vkGetPhysicalDeviceQueueFamilyProperties(v->phys, &nq, NULL);
    if (nq > 16) nq = 16;
    vkGetPhysicalDeviceQueueFamilyProperties(v->phys, &nq, qf);
    v->queueFamily = UINT32_MAX;
    for (uint32_t i = 0; i < nq; i++)
        if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            v->queueFamily = i;
            break;
        }
    if (v->queueFamily == UINT32_MAX) return -1;

    VkPhysicalDeviceFeatures have;
    vkGetPhysicalDeviceFeatures(v->phys, &have);
    v->anisotropy = have.samplerAnisotropy;

    const char* exts[] = {VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME};
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = v->queueFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    VkPhysicalDeviceDynamicRenderingFeaturesKHR dr = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR};
    dr.dynamicRendering = VK_TRUE;
    VkPhysicalDeviceVulkan11Features f11 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    f11.pNext = &dr;
    f11.samplerYcbcrConversion = VK_TRUE;
    VkPhysicalDeviceFeatures2 f2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    f2.pNext = &f11;
    f2.features.samplerAnisotropy = have.samplerAnisotropy;
    f2.features.textureCompressionASTC_LDR = have.textureCompressionASTC_LDR;
    VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.pNext = &f2;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 2;
    dci.ppEnabledExtensionNames = exts;
    VkResult r = vkCreateDevice(v->phys, &dci, NULL, &v->device);
    if (r != VK_SUCCESS) {
        fprintf(stderr, "vkCreateDevice failed: %d\n", r);
        return -1;
    }
    vkGetDeviceQueue(v->device, v->queueFamily, 0, &v->queue);
    VkCommandPoolCreateInfo pci = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = v->queueFamily;
    if (vkCreateCommandPool(v->device, &pci, NULL, &v->pool) != VK_SUCCESS) return -1;
    VkCommandBufferAllocateInfo cai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = v->pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(v->device, &cai, &v->cmd) != VK_SUCCESS) return -1;
    VkFenceCreateInfo fci = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (vkCreateFence(v->device, &fci, NULL, &v->fence) != VK_SUCCESS) return -1;
    return 0;
}

void vkh_destroy(Vkh* v) {
    if (v->device) {
        vkDeviceWaitIdle(v->device);
        if (v->fence) vkDestroyFence(v->device, v->fence, NULL);
        if (v->pool) vkDestroyCommandPool(v->device, v->pool, NULL);
        vkDestroyDevice(v->device, NULL);
    }
    if (v->instance) vkDestroyInstance(v->instance, NULL);
    memset(v, 0, sizeof(*v));
}

VkCommandBuffer vkh_begin(Vkh* v) {
    vkResetCommandBuffer(v->cmd, 0);
    VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(v->cmd, &bi);
    return v->cmd;
}

int vkh_submit(Vkh* v) {
    if (vkEndCommandBuffer(v->cmd) != VK_SUCCESS) return -1;
    VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &v->cmd;
    if (vkQueueSubmit(v->queue, 1, &si, v->fence) != VK_SUCCESS) return -1;
    VkResult r = vkWaitForFences(v->device, 1, &v->fence, VK_TRUE, 5000000000ull);
    vkResetFences(v->device, 1, &v->fence);
    return r == VK_SUCCESS ? 0 : -1;
}

int vkh_buffer(Vkh* v, VkDeviceSize size, VkBufferUsageFlags usage, VkhBuffer* b) {
    memset(b, 0, sizeof(*b));
    VkBufferCreateInfo bi = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size;
    bi.usage = usage;
    if (vkCreateBuffer(v->device, &bi, NULL, &b->buffer) != VK_SUCCESS) return -1;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(v->device, b->buffer, &req);
    VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex =
        memType(v, req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(v->device, &ai, NULL, &b->mem) != VK_SUCCESS) return -1;
    vkBindBufferMemory(v->device, b->buffer, b->mem, 0);
    vkMapMemory(v->device, b->mem, 0, VK_WHOLE_SIZE, 0, &b->map);
    b->size = size;
    return 0;
}

void vkh_buffer_free(Vkh* v, VkhBuffer* b) {
    if (b->mem) vkUnmapMemory(v->device, b->mem);
    if (b->buffer) vkDestroyBuffer(v->device, b->buffer, NULL);
    if (b->mem) vkFreeMemory(v->device, b->mem, NULL);
    memset(b, 0, sizeof(*b));
}

static int isDepth(VkFormat f) {
    return f == VK_FORMAT_D32_SFLOAT || f == VK_FORMAT_D16_UNORM || f == VK_FORMAT_D24_UNORM_S8_UINT ||
           f == VK_FORMAT_D32_SFLOAT_S8_UINT;
}

static int makeImage(Vkh* v, uint32_t w, uint32_t h, VkFormat format, VkFormat viewFormat, uint32_t mips,
                     VkhImage* img) {
    memset(img, 0, sizeof(*img));
    int planar = format == VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
    VkImageCreateInfo ci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = format;
    ci.extent = (VkExtent3D){w, h, 1};
    ci.mipLevels = mips;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    if (isDepth(format))
        ci.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    else if (planar)
        ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    else
        ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (viewFormat != format) ci.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    if (vkCreateImage(v->device, &ci, NULL, &img->image) != VK_SUCCESS) return -1;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(v->device, img->image, &req);
    VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = memType(v, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(v->device, &ai, NULL, &img->mem) != VK_SUCCESS) return -1;
    vkBindImageMemory(v->device, img->image, img->mem, 0);
    img->format = viewFormat;
    img->width = w;
    img->height = h;
    img->mips = mips;
    img->layout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (planar) return 0;
    VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = img->image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = viewFormat;
    vi.subresourceRange =
        (VkImageSubresourceRange){isDepth(format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT, 0, mips, 0, 1};
    return vkCreateImageView(v->device, &vi, NULL, &img->view) == VK_SUCCESS ? 0 : -1;
}

int vkh_image(Vkh* v, uint32_t w, uint32_t h, VkFormat format, VkhImage* img) {
    return makeImage(v, w, h, format, format, 1, img);
}

int vkh_image_mutable(Vkh* v, uint32_t w, uint32_t h, VkFormat format, VkFormat viewFormat, VkhImage* img) {
    return makeImage(v, w, h, format, viewFormat, 1, img);
}

int vkh_image_mips(Vkh* v, uint32_t w, uint32_t h, VkFormat format, uint32_t mips, VkhImage* img, VkImageView* level0) {
    if (makeImage(v, w, h, format, format, mips, img)) return -1;
    VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = img->image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    vi.subresourceRange = (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    return vkCreateImageView(v->device, &vi, NULL, level0) == VK_SUCCESS ? 0 : -1;
}

void vkh_image_free(Vkh* v, VkhImage* img) {
    if (img->view) vkDestroyImageView(v->device, img->view, NULL);
    if (img->image) vkDestroyImage(v->device, img->image, NULL);
    if (img->mem) vkFreeMemory(v->device, img->mem, NULL);
    memset(img, 0, sizeof(*img));
}

void vkh_layout(Vkh* v, VkhImage* img, VkImageLayout to) {
    VkImageAspectFlags aspect = isDepth(img->format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    mlxr_barrier(v->cmd, img->image, aspect, img->layout, to);
    img->layout = to;
}

int vkh_upload(Vkh* v, VkhImage* img, VkImageAspectFlagBits aspect, uint32_t w, uint32_t h, uint32_t bpp,
               const void* px) {
    VkhBuffer b;
    if (vkh_buffer(v, (VkDeviceSize)w * h * bpp, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &b)) return -1;
    memcpy(b.map, px, (size_t)w * h * bpp);
    VkCommandBuffer cmd = vkh_begin(v);
    VkImageAspectFlags whole = VK_IMAGE_ASPECT_COLOR_BIT;
    mlxr_barrier(cmd, img->image, whole, img->layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy rg = {0, 0, 0, {aspect, 0, 0, 1}, {0, 0, 0}, {w, h, 1}};
    vkCmdCopyBufferToImage(cmd, b.buffer, img->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &rg);
    mlxr_barrier(cmd, img->image, whole, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    img->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    int r = vkh_submit(v);
    vkh_buffer_free(v, &b);
    return r;
}

int vkh_read(Vkh* v, VkhImage* img, uint8_t* out) {
    VkhBuffer b;
    VkDeviceSize bytes = (VkDeviceSize)img->width * img->height * 4;
    if (vkh_buffer(v, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &b)) return -1;
    VkCommandBuffer cmd = vkh_begin(v);
    mlxr_barrier(cmd, img->image, VK_IMAGE_ASPECT_COLOR_BIT, img->layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy rg = {0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {img->width, img->height, 1}};
    vkCmdCopyImageToBuffer(cmd, img->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, b.buffer, 1, &rg);
    mlxr_barrier(cmd, img->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    img->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    int r = vkh_submit(v);
    if (r == 0) memcpy(out, b.map, (size_t)bytes);
    vkh_buffer_free(v, &b);
    return r;
}

int vkh_write_ppm(const char* path, const uint8_t* rgba, uint32_t w, uint32_t h) {
    FILE* f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (size_t i = 0; i < (size_t)w * h; i++) fwrite(rgba + i * 4, 1, 3, f);
    fclose(f);
    return 0;
}
