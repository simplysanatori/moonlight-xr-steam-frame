// Moonlight XR Native: Vulkan device created through XR_KHR_vulkan_enable2,
// plus small helpers. SteamVR 2.18 accepts a Vulkan 1.2 instance only;
// newer features are enabled as extensions. GPLv3.
#pragma once
#include <vulkan/vulkan.h>
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

typedef struct XrVk {
    VkInstance instance;
    VkPhysicalDevice phys;
    VkDevice device;
    uint32_t queueFamily;
    VkQueue queue;
    VkPhysicalDeviceMemoryProperties mem;
    PFN_vkCmdBeginRenderingKHR cmdBeginRendering;
    PFN_vkCmdEndRenderingKHR cmdEndRendering;
} XrVk;

// extraDeviceExts: more device extensions to enable (e.g. the decoder import
// lane's list); may be NULL.
int xr_vk_create(XrInstance instance, XrSystemId system, const char* const* extraDeviceExts, uint32_t nExtra,
                 XrVk* vk);
void xr_vk_destroy(XrVk* vk);

uint32_t xr_vk_memory_type(const XrVk* vk, uint32_t typeBits, VkMemoryPropertyFlags want);
VkShaderModule xr_vk_shader(const XrVk* vk, const uint32_t* code, size_t bytes);
void xr_vk_image_barrier(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to,
                         VkAccessFlags srcAccess, VkAccessFlags dstAccess, VkPipelineStageFlags srcStage,
                         VkPipelineStageFlags dstStage);
