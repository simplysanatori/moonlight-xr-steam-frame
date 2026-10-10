// Moonlight XR Native: Vulkan through XR_KHR_vulkan_enable2. GPLv3.
#include "xr_vk.h"

#include <stdio.h>
#include <string.h>

#include "mlxr_log.h"

#define XRF(name) PFN_##name name = NULL; xrGetInstanceProcAddr(instance, #name, (PFN_xrVoidFunction*)&name)

int xr_vk_create(XrInstance instance, XrSystemId system, const char* const* extraDeviceExts, uint32_t nExtra,
                 XrVk* vk) {
    memset(vk, 0, sizeof(*vk));
    XRF(xrGetVulkanGraphicsRequirements2KHR);
    XRF(xrCreateVulkanInstanceKHR);
    XRF(xrGetVulkanGraphicsDevice2KHR);
    XRF(xrCreateVulkanDeviceKHR);
    if (!xrGetVulkanGraphicsRequirements2KHR || !xrCreateVulkanInstanceKHR || !xrGetVulkanGraphicsDevice2KHR ||
        !xrCreateVulkanDeviceKHR) {
        MLOG_E("XR_KHR_vulkan_enable2 entry points missing");
        return -1;
    }
    XrGraphicsRequirementsVulkan2KHR req = {XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};
    if (XR_FAILED(xrGetVulkanGraphicsRequirements2KHR(instance, system, &req))) return -1;

    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Moonlight XR Native";
    app.applicationVersion = 1;
    app.pEngineName = "mlxr";
    app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    XrVulkanInstanceCreateInfoKHR xci = {XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
    xci.systemId = system;
    xci.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    xci.vulkanCreateInfo = &ici;
    VkResult vr = VK_SUCCESS;
    if (XR_FAILED(xrCreateVulkanInstanceKHR(instance, &xci, &vk->instance, &vr)) || vr != VK_SUCCESS) {
        MLOG_E("xrCreateVulkanInstanceKHR failed (vk %d)", vr);
        return -1;
    }
    XrVulkanGraphicsDeviceGetInfoKHR gi = {XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
    gi.systemId = system;
    gi.vulkanInstance = vk->instance;
    if (XR_FAILED(xrGetVulkanGraphicsDevice2KHR(instance, &gi, &vk->phys))) return -1;
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(vk->phys, &props);
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

    const char* exts[32];
    uint32_t n = 0;
    exts[n++] = VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME;
    exts[n++] = VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME; // mlxr_passes binds textures as push descriptors
    for (uint32_t i = 0; i < nExtra && n < 32; i++) exts[n++] = extraDeviceExts[i];
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = vk->queueFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    VkPhysicalDeviceDynamicRenderingFeaturesKHR dr = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR};
    dr.dynamicRendering = VK_TRUE;
    VkPhysicalDeviceVulkan11Features f11 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    f11.pNext = &dr;
    f11.samplerYcbcrConversion = VK_TRUE;
    VkPhysicalDeviceVulkan12Features f12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    f12.pNext = &f11;
    f12.timelineSemaphore = VK_TRUE;
    f12.hostQueryReset = VK_TRUE;
    VkPhysicalDeviceFeatures2 f2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    f2.pNext = &f12;
    f2.features.textureCompressionASTC_LDR = VK_TRUE; // room atlases ship as ASTC
    f2.features.samplerAnisotropy = VK_TRUE;
    VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.pNext = &f2;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = n;
    dci.ppEnabledExtensionNames = exts;
    XrVulkanDeviceCreateInfoKHR xdci = {XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
    xdci.systemId = system;
    xdci.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    xdci.vulkanPhysicalDevice = vk->phys;
    xdci.vulkanCreateInfo = &dci;
    if (XR_FAILED(xrCreateVulkanDeviceKHR(instance, &xdci, &vk->device, &vr)) || vr != VK_SUCCESS) {
        MLOG_E("xrCreateVulkanDeviceKHR failed (vk %d)", vr);
        return -1;
    }
    vkGetDeviceQueue(vk->device, vk->queueFamily, 0, &vk->queue);
    vk->cmdBeginRendering = (PFN_vkCmdBeginRenderingKHR)vkGetDeviceProcAddr(vk->device, "vkCmdBeginRenderingKHR");
    vk->cmdEndRendering = (PFN_vkCmdEndRenderingKHR)vkGetDeviceProcAddr(vk->device, "vkCmdEndRenderingKHR");
    MLOG_I("vulkan: %s, device api %u.%u, runtime accepts %u.%u..%u.%u, %u extra device extensions",
           props.deviceName, VK_API_VERSION_MAJOR(props.apiVersion), VK_API_VERSION_MINOR(props.apiVersion),
           XR_VERSION_MAJOR(req.minApiVersionSupported), XR_VERSION_MINOR(req.minApiVersionSupported),
           XR_VERSION_MAJOR(req.maxApiVersionSupported), XR_VERSION_MINOR(req.maxApiVersionSupported), nExtra);
    return 0;
}

void xr_vk_destroy(XrVk* vk) {
    if (vk->device) vkDestroyDevice(vk->device, NULL);
    if (vk->instance) vkDestroyInstance(vk->instance, NULL);
    memset(vk, 0, sizeof(*vk));
}

uint32_t xr_vk_memory_type(const XrVk* vk, uint32_t typeBits, VkMemoryPropertyFlags want) {
    for (uint32_t i = 0; i < vk->mem.memoryTypeCount; i++)
        if ((typeBits & (1u << i)) && (vk->mem.memoryTypes[i].propertyFlags & want) == want) return i;
    return UINT32_MAX;
}

VkShaderModule xr_vk_shader(const XrVk* vk, const uint32_t* code, size_t bytes) {
    VkShaderModuleCreateInfo ci = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = bytes;
    ci.pCode = code;
    VkShaderModule m = VK_NULL_HANDLE;
    vkCreateShaderModule(vk->device, &ci, NULL, &m);
    return m;
}

void xr_vk_image_barrier(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to,
                         VkAccessFlags srcAccess, VkAccessFlags dstAccess, VkPipelineStageFlags srcStage,
                         VkPipelineStageFlags dstStage) {
    VkImageMemoryBarrier b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = srcAccess;
    b.dstAccessMask = dstAccess;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, NULL, 0, NULL, 1, &b);
}
