// Moonlight XR Native: a headless Vulkan 1.2 device and image helpers for
// the shader tests. GPLv3.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

typedef struct Vkh {
    VkInstance instance;
    VkPhysicalDevice phys;
    VkDevice device;
    uint32_t queueFamily;
    VkQueue queue;
    VkCommandPool pool;
    VkCommandBuffer cmd;
    VkFence fence;
    VkPhysicalDeviceProperties props;
    int anisotropy;
} Vkh;

typedef struct VkhImage {
    VkImage image;
    VkDeviceMemory mem;
    VkImageView view;
    VkFormat format;
    uint32_t width, height, mips;
    VkImageLayout layout;
} VkhImage;

typedef struct VkhBuffer {
    VkBuffer buffer;
    VkDeviceMemory mem;
    void* map;
    VkDeviceSize size;
} VkhBuffer;

// 0 on success; VKH_NO_DEVICE when there is no Vulkan driver or device at all
// (a CI runner without lavapipe): the test then exits 77, which ctest counts
// as skipped
#define VKH_NO_DEVICE 77
int vkh_init(Vkh* v);
void vkh_destroy(Vkh* v);
VkCommandBuffer vkh_begin(Vkh* v);
int vkh_submit(Vkh* v); // ends, submits and waits

int vkh_buffer(Vkh* v, VkDeviceSize size, VkBufferUsageFlags usage, VkhBuffer* b);
void vkh_buffer_free(Vkh* v, VkhBuffer* b);

// A sampled + colour attachment + transfer image (or depth attachment for a
// depth format); view NULL for multi-planar formats (the caller makes it)
int vkh_image(Vkh* v, uint32_t w, uint32_t h, VkFormat format, VkhImage* img);
// The same with a view in another format of the same image (MUTABLE_FORMAT)
int vkh_image_mutable(Vkh* v, uint32_t w, uint32_t h, VkFormat format, VkFormat viewFormat, VkhImage* img);
// A colour image with a mip chain: img->view covers every level, *level0
// (the caller destroys it) the first alone, to render into
int vkh_image_mips(Vkh* v, uint32_t w, uint32_t h, VkFormat format, uint32_t mips, VkhImage* img, VkImageView* level0);
void vkh_image_free(Vkh* v, VkhImage* img);
// Moves the image to the layout (records into v->cmd, which must be begun)
void vkh_layout(Vkh* v, VkhImage* img, VkImageLayout to);
// Upload tightly packed pixels of one plane (aspect COLOR or PLANE_0/1),
// whole command buffer round trip; leaves the image SHADER_READ_ONLY
int vkh_upload(Vkh* v, VkhImage* img, VkImageAspectFlagBits aspect, uint32_t w, uint32_t h, uint32_t bpp,
               const void* px);
// Read back an RGBA8 (4 bytes a pixel) image; leaves it SHADER_READ_ONLY
int vkh_read(Vkh* v, VkhImage* img, uint8_t* out);

int vkh_write_ppm(const char* path, const uint8_t* rgba, uint32_t w, uint32_t h);
