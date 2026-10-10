// Moonlight XR Native, tests/m5: a headless Vulkan device shaped like the
// session's (xr_vk.c's features and extensions, no OpenXR), the shader
// passes, and small helpers to make, fill and read back images, so the ported
// room, glow and model code runs on the Frame's GPU without a headset. GPLv3.
#pragma once
#include "xr_world.h"

typedef struct M5Gpu {
    XrVk vk;
    VkCommandPool pool;
    MlxrPasses* passes;
    VkPhysicalDeviceProperties props;
    VkSemaphore timeline;
    uint64_t serial;
} M5Gpu;

// 0 on success
int m5_gpu_init(M5Gpu* g);
void m5_gpu_free(M5Gpu* g);
VkCommandBuffer m5_begin(M5Gpu* g);
// Submits extra (may be 0 of them) then cmd, signals ++serial, waits; frees cmd
int m5_submit(M5Gpu* g, VkCommandBuffer cmd, const VkCommandBuffer* extra, int nExtra);

// A context ready for the world code: the device, the passes, the assets,
// offscreen (no session), the recommended eye size of the Frame (1728)
void m5_ctx_gpu(M5Gpu* g, XrCtx* ctx);
// One frame of the world code: begin, xrWorldFrameBegin, body(user), the
// loader's uploads ahead, submit, wait, gpuDone
void m5_frame(M5Gpu* g, XrCtx* ctx, void (*body)(XrCtx* ctx, VkCommandBuffer cmd, void* user), void* user);

typedef struct M5Image {
    XrVkImage img;
    int w, h;
} M5Image;
int m5_image(M5Gpu* g, XrCtx* ctx, M5Image* out, int w, int h, VkFormat format, VkImageUsageFlags usage,
             VkImageAspectFlags aspect);
void m5_image_free(XrCtx* ctx, M5Image* im);
// Reads an RGBA8 colour image (in `layout`) into rgba (w*h*4)
int m5_readback(M5Gpu* g, VkImage image, VkImageLayout layout, int w, int h, uint8_t* rgba);
// Uploads tight bytes (bpp bytes a texel) into an image, leaving it SHADER_READ
int m5_upload(M5Gpu* g, VkImage image, int w, int h, int bpp, const void* bytes);

// A software video frame (three R8 planes, BT.709 limited) of an RGB picture
// rgb(u, v) -> 0..1, as the decoder would hand it over
typedef struct M5Video {
    M5Image planes[3];
    MlxrVideo video;
} M5Video;
int m5_video(M5Gpu* g, XrCtx* ctx, M5Video* v, int w, int h, void (*rgb)(float u, float v, float out[3]));
void m5_video_free(XrCtx* ctx, M5Video* v);

// A symmetric view from the origin looking down -z, hfov degrees wide, w x h
XrView m5_view(float hfovDeg, int w, int h, float eyeX);
