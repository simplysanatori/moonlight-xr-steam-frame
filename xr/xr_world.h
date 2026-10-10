// Moonlight XR Native: the GPU side the ported room, glow and controller
// models share (xr_world.c). Upstream's GL code made its swapchains, buffers
// and textures with the GL context current and let the driver order the work;
// natively each of them records into the frame's one command buffer
// (ctx->frameCmd), images acquired on the way are released by the host after
// the submission, and anything a frame in flight may still read is freed only
// once the GPU has passed the serial that last used it. GPLv3.
#ifndef XR_WORLD_H
#define XR_WORLD_H

#include "xr_renderer.h"
#include "passes.h"
#include "room_gpu.h"
#include "xr_vk.h"

// ---- small helpers ---------------------------------------------------------------
// An image of the app's own with one view, left UNDEFINED
int xrVkImageMake(XrCtx* ctx, XrVkImage* img, int w, int h, VkFormat format, VkImageUsageFlags usage,
                  VkImageAspectFlags aspect);
void xrVkImageFree(XrCtx* ctx, XrVkImage* img);
// Moves an image to a layout from the one it was left in (ctx->frameCmd)
void xrVkImageTo(XrCtx* ctx, XrVkImage* img, VkImageAspectFlags aspect, VkImageLayout to);
// The rooms' loader's view of the device
MlxrGpu xrWorldGpu(XrCtx* ctx);

// Upstream's createArtSwapchain: an R8G8B8A8_SRGB swapchain made mid session
// (counted in swapchainsAlive), its images and a view of each
int createArtSwapchain(XrCtx* ctx, int width, int height, const char* what, XrSwapchain* out,
                       XrSwapchainImageVulkan2KHR** images, VkImageView** views, uint32_t* count);
void destroyArtSwapchain(XrCtx* ctx, XrSwapchain* swapchain, XrSwapchainImageVulkan2KHR** images,
                         VkImageView** views, uint32_t* count);
// Acquires and waits for an image (0 on failure, nothing left acquired)
int xrArtAcquire(XrSwapchain swapchain, const char* what, uint32_t* index);

// The depth image the room and the models draw with, at least w x h, in
// DEPTH_STENCIL_ATTACHMENT_OPTIMAL after this (ordered after any earlier
// depth pass of the frame)
VkImageView xrWorldDepth(XrCtx* ctx, int w, int h);

// ---- the frame -------------------------------------------------------------------------
// Where the rooms and the controller model are read from: ./assets (the
// installed app runs from its own directory), else beside the binary, else
// the source tree's
void xrWorldFindAssets(XrCtx* ctx);
// Once the frame's command buffer is recording (ctx->frameCmd set) and the
// GPU has finished up to ctx->gpuDone: frees what is no longer in use, reads
// the last frame's timings, brings up the ambilight's images and the
// controller model, and gets the room ready (prepareRoom)
void xrWorldFrameBegin(XrCtx* ctx);
// Uploads recorded off the frame loop that must go ahead of the frame's own
// command buffer, in this submission; then the serial it signalled
int xrWorldTakeUploads(XrCtx* ctx, VkCommandBuffer* out, int max);
void xrWorldSubmitted(XrCtx* ctx, uint64_t serial);
// Releases the room's, the models' and the glow's swapchain images acquired
// this frame (after the submission that drew them)
void xrWorldReleaseImages(XrCtx* ctx);
// Everything, at the end of the session (the device idle)
void xrWorldDestroy(XrCtx* ctx);

// A new picture was drawn (upstream renderVideoFrame's ambilight part): the
// colour sample when the glow or a room wants it, the glow, the letterbox
// detection. A picture standing still: the glow drawn again when its switch
// or level moved (redrawGlow). Both record into ctx->frameCmd.
void xrWorldVideoFrame(XrCtx* ctx, const MlxrVideo* video);
void xrWorldStillFrame(XrCtx* ctx, const MlxrVideo* video);
// The stream stopped: nothing sampled from it stays
void xrWorldVideoGone(XrCtx* ctx);
// Whether the glow goes up flat this frame, and if so copies the newest glow
// into its swapchain (the quad layer's image)
void xrWorldFlatGlow(XrCtx* ctx, int curved);
// Whether the glow shows at all now (switch, level, a picture up)
int xrWorldGlowShown(XrCtx* ctx);
// The glow drawn on the curved picture's surface, a little proud of it, into
// a projection layer target (the curved picture's own; native, there being no
// cylinder layers): one draw per eye
void xrWorldCurvedGlow(XrCtx* ctx, const MlxrTarget* target, const XrView* views, int eyeW, int eyeH);
// Native: the flat glow drawn the same way, into the picture's own projection
// layer under the picture (flat in 3D): SteamVR puts quad layers over a
// projection layer whatever their order, so the glow quad would cover it
void xrWorldFlatGlowInto(XrCtx* ctx, const MlxrTarget* target, const XrView* views, int eyeW, int eyeH);

// The room's own pass into any target (renderRoom's draw; the snapshot tool
// draws through it too). colour in COLOR_ATTACHMENT_OPTIMAL, depth in
// DEPTH_STENCIL_ATTACHMENT_OPTIMAL.
void xrRoomRecord(XrCtx* ctx, VkCommandBuffer cmd, VkImageView colour, VkFormat format, VkImageView depth,
                  int eyeW, int eyeH, const XrView* views);
// The models' pass the same way
void xrModelRecord(XrCtx* ctx, VkCommandBuffer cmd, VkImageView colour, VkFormat format, VkImageView depth,
                   int eyeW, int eyeH, const XrView* views);
// The resident room's style (0 none) and whether a load is under way
int xrRoomResident(XrCtx* ctx);
int xrRoomLoading(XrCtx* ctx);
// Rooms freed since the start and rooms waiting on the GPU (tests)
int xrRoomsRetired(XrCtx* ctx);
// Native, for the rooms: let the loader finish and adopt what it made
// (tests, the snapshot tool; the frame loop polls instead)
void xrRoomWaitLoader(XrCtx* ctx);

// ---- GPU time ----------------------------------------------------------------------------
// M6: the depth model's capture (downscale + readback copy), the depth
// upsample and the offset search have their own
enum { GPU_WARP, GPU_GLOW, GPU_ROOM, GPU_MODELS, GPU_CURVE, GPU_DOWNSCALE, GPU_UPSAMPLE, GPU_OFFSET, GPU_SECTIONS };
void xrGpuTimeBegin(XrCtx* ctx, int section);
void xrGpuTimeEnd(XrCtx* ctx, int section);
// The averages since the last call, for the stats line ("" when none)
void xrGpuTimesLine(XrCtx* ctx, char* buf, int len);
// The averages of one section since the last xrGpuTimesLine (0 when none)
double xrGpuTimeAvg(XrCtx* ctx, int section, int* samples);

// ---- the click (xr_session.c sets the player) --------------------------------------------
// The input pass asked for a click this frame (IN_CLICK) and the setting is on
// (upstream XrRenderer.updateClickSound): calls the player, 1 if it did
extern void (*xrClickPlayer)(void);
int xrClickFrame(XrCtx* ctx, const float* in);

#endif
